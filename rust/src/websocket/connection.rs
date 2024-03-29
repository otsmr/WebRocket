use crate::base64;
use crate::dataframe::{ControlCloseCode, DataFrame, Opcode};
use crate::http_parser::{parse_http_header, HttpHeader};
use crate::sha1::sha1;
use crate::websocket::packet::Packet;
use log::debug;
use std::collections::HashMap;
use std::io;
use std::io::ErrorKind;
use std::time::Duration;
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::net::TcpStream;

#[derive(PartialEq, Eq, Debug, PartialOrd, Ord)]
enum ConnectionState {
    Disconnected = 0,
    CloseFromServer,
    WaitingForConnection = 10,
    WaitingForFrames,
    InDataPayload, // with socket.write each time only 1024 bytes
}

// enum _CloseCode {
//     Reserved1 = 1004,
//     Reserved2 = 1005,
//     Reserved3 = 1006,
//     TLSError = 1015
// }
// type close_fkt
//
//
type MsgCallback = fn(&mut Connection, &String) -> ();

pub struct Connection {
    socket: TcpStream,
    state: ConnectionState,
    waiting_for_pong: bool,
    send_queue: Vec<DataFrame>,
    send_queue_high_prio: Vec<DataFrame>,
    request_header: Option<HttpHeader>,
    on_message_fkt: Vec<MsgCallback>,
    on_fkts: std::collections::HashMap<String, Vec<MsgCallback>>,
    on_bytes_fkt: Vec<fn(&mut Self, &Vec<u8>) -> ()>,
    on_close_fkt: Vec<fn(&mut Self, ControlCloseCode, &String) -> ()>,
}

impl Connection {
    pub fn new(socket: TcpStream) -> Self {
        Connection {
            socket,
            state: ConnectionState::WaitingForConnection,
            waiting_for_pong: false,
            request_header: None,
            on_message_fkt: vec![],
            on_fkts: HashMap::new(),
            on_close_fkt: vec![],
            on_bytes_fkt: vec![],
            send_queue: vec![],
            send_queue_high_prio: vec![],
        }
    }
    pub fn on_bytes(&mut self, f: fn(&mut Self, &Vec<u8>) -> ()) {
        self.on_bytes_fkt.push(f);
    }
    pub fn on_message(&mut self, f: MsgCallback) {
        self.on_message_fkt.push(f);
    }
    pub fn on(&mut self, event: String, f: MsgCallback) {
        let entry = self.on_fkts.entry(event).or_default();
        entry.push(f.to_owned());
    }
    pub fn on_close(&mut self, f: fn(&mut Self, ControlCloseCode, &String) -> ()) {
        self.on_close_fkt.push(f);
    }
    pub fn send_bytes(&mut self, bytes: &Vec<u8>) {
        let frame = DataFrame::bytes(bytes.to_owned());
        self.send_frame(frame);
    }
    pub fn send_message(&mut self, message: String) {
        self.send_frame(DataFrame::text(message));
    }
    pub fn emit(&mut self, event: String, payload: String) {
        self.send_message(Packet::to_string(event, payload));
    }
    pub fn send_frame(&mut self, frame: DataFrame) {
        self.send_queue.push(frame);
    }
    fn send_ping(&mut self) {
        let pong = DataFrame::ping();
        self.waiting_for_pong = true;
        self.send_queue_high_prio.push(pong);
    }
    fn send_pong(&mut self, frame: &DataFrame) {
        let mut pong = DataFrame::pong();
        if !frame.payload.is_empty() {
            pong.payload = frame.payload.clone();
        }
        self.send_queue_high_prio.push(pong);
    }
    pub fn close(&mut self, statuscode: ControlCloseCode) {
        self.state = ConnectionState::CloseFromServer;
        let frame = DataFrame::closing(statuscode);
        self.send_queue.push(frame);
    }
    async fn handle_raw_data(
        &mut self,
        buf: &[u8],
        frames: &mut Vec<DataFrame>,
    ) -> io::Result<(bool, usize)> {
        let mut offset: Option<usize> = None;
        match self.state {
            ConnectionState::WaitingForFrames => {
                let frame = DataFrame::from_raw(buf);
                if let Ok(frame) = frame {
                    frames.push(frame);
                    if !frames.last().unwrap().is_full() {
                        self.state = ConnectionState::InDataPayload;
                        return Ok((false, frames.last().unwrap().current_len()));
                    }
                } else if let Err(close_code) = frame {
                    if let Some(close_code) = close_code {
                        self.close(close_code);
                        return Ok((true, buf.len()));
                    }
                    return Ok((false, 0));
                }
            }
            ConnectionState::InDataPayload => {
                let used_bytes = frames.last_mut().unwrap().add_payload(buf);
                if !frames.last().unwrap().is_full() {
                    return Ok((false, used_bytes));
                }
                offset = Some(used_bytes);
                self.state = ConnectionState::WaitingForFrames;
            }
            ConnectionState::WaitingForConnection => {
                let http_response = self.handle_handshake(buf);
                if self.socket.write_all(&http_response.as_vec()).await.is_ok()
                    && http_response.status_code == 101
                {
                    self.state = ConnectionState::WaitingForFrames;
                    return Ok((false, buf.len()));
                }
                return Err(io::Error::new(
                    ErrorKind::InvalidData,
                    "Invalid handshake request",
                ));
            }
            _ => return Ok((true, 0)),
        }

        let frame = frames.last().unwrap();

        log::debug!("Got frame with opcode: {:?}", frame.opcode);

        if frame.opcode >= Opcode::new(0x8).unwrap() {
            // Control frames can be interjected in
            // the middle of a fragmented message.
            let frame = frames.pop().unwrap();

            if frame.payload.len() > 125 || !frame.flags.fin {
                self.close(ControlCloseCode::ProtocolError);
                return Ok((true, buf.len()));
            }

            match frame.opcode {
                Opcode::ConectionClose => {
                    let mut statuscode = frame.get_closing_code();
                    let close_reason;
                    let parse_reason = frame.get_closing_reason();
                    if let Err(error_statuscode) = parse_reason {
                        statuscode = error_statuscode;
                        close_reason = "".to_string();
                    } else {
                        close_reason = parse_reason.ok().unwrap();
                    }
                    if !self.on_close_fkt.is_empty() {
                        let on_closes = self.on_close_fkt.clone();
                        for on_close in on_closes {
                            on_close(self, statuscode, &close_reason);
                        }
                    }
                    if self.state == ConnectionState::CloseFromServer {
                        self.state = ConnectionState::Disconnected;
                        return Ok((true, 0));
                    }
                    let frame = DataFrame::closing(statuscode);
                    self.socket.write_all(frame.as_bytes().as_slice()).await?;

                    self.state = ConnectionState::Disconnected;
                    return Ok((true, 0));
                }
                Opcode::Ping => self.send_pong(&frame),
                Opcode::Pong => self.waiting_for_pong = false,
                _ => (),
            }
            return Ok((false, frame.current_len()));
        }

        if frames.len() > 1 && frame.opcode != Opcode::ContinuationFrame {
            self.close(ControlCloseCode::ProtocolError);
            return Ok((true, buf.len()));
        }

        let frame_type = if frame.opcode == Opcode::ContinuationFrame {
            if frames.len() == 1 {
                // The connection is failed immediately, since there is no message to continue.
                self.close(ControlCloseCode::ProtocolError);
                return Ok((true, buf.len()));
            }
            frames[0].opcode
        } else {
            frame.opcode
        };

        let offset = match offset {
            Some(o) => o,
            None => frame.current_len(),
        };

        if !frame.flags.fin {
            return Ok((false, offset));
        }

        match frame_type {
            Opcode::TextFrame => {
                let string = DataFrame::payload_as_string(frames);
                if let Ok(string) = string {
                    if !self.on_message_fkt.is_empty() {
                        let on_messages = self.on_message_fkt.clone();
                        for on_message in on_messages.iter() {
                            on_message(self, &string.to_owned());
                        }
                    }
                    let packet = Packet::from_string(string);
                    let callbacks = self.on_fkts.entry(packet.event).or_default().clone();
                    for callback in callbacks {
                        callback(self, &packet.payload.to_owned());
                    }
                } else {
                    self.close(ControlCloseCode::InvalidData);
                    return Ok((true, buf.len()));
                }
            }
            Opcode::BinaryFrame => {
                let bytes = DataFrame::payload_as_bytes(frames);
                let on_bytes = self.on_bytes_fkt.clone();
                for on_byte in on_bytes.iter() {
                    on_byte(self, &bytes);
                }
            }
            // Opcode::BinaryFrame => match DataFrame::frames_as_bytes(frames) {
            //     Ok(bytes) => {
            //         let on_bytes = self.on_bytes_fkt.clone();
            //         for on_byte in on_bytes.iter() {
            //             on_byte(self, &bytes);
            //         }
            //     }
            //     Err(e) => {
            //         self.close(ControlCloseCode::InvalidData);
            //         return Ok((true, buf.len()));
            //     }
            // }
            o => log::warn!("Opcode ({:?}) not implemented!", o),
        }

        frames.clear();
        Ok((false, offset))
    }

    pub async fn connect(&mut self) -> io::Result<()> {
        debug!("Trying to connect with WebSocket");

        let mut temp_buffer = vec![];
        let mut read_buffer = [0; 1024];
        let mut frames = Vec::<DataFrame>::new();
        let mut close_connection = false;

        loop {
            if self.state > ConnectionState::WaitingForConnection {
                for df in self.send_queue_high_prio.iter() {
                    debug!("Send frame {:?}", df.opcode);
                    self.socket.write_all(df.as_bytes().as_slice()).await?;
                }

                self.send_queue_high_prio.clear();
                for df in self.send_queue.iter() {
                    debug!("Send frame {:?}", df.opcode);
                    self.socket.write_all(df.as_bytes().as_slice()).await?;
                }

                self.send_queue.clear();
            }

            if close_connection {
                break;
            }

            let rx = self.socket.read(&mut read_buffer);

            let read_buffer_len: usize = if self.state == ConnectionState::CloseFromServer {
                tokio::time::timeout(Duration::from_secs(2), rx).await??
            } else {
                match tokio::time::timeout(Duration::from_secs(60), rx).await {
                    Ok(n) => n?,
                    Err(_) => {
                        if self.waiting_for_pong {
                            break; // close tcp socket because of timeout
                        }
                        self.send_ping();
                        continue;
                    }
                }
            };

            if read_buffer_len == 0 {
                self.state = ConnectionState::Disconnected;
                break;
            }

            let mut offset = 0;

            while offset < read_buffer_len && !close_connection {
                let current_offset;

                let mut using_buffer = &read_buffer[offset..read_buffer_len];
                let mut last_offset = 0;

                if !temp_buffer.is_empty() {
                    last_offset = temp_buffer.len();
                    temp_buffer.append(&mut using_buffer.to_vec());
                    using_buffer = temp_buffer.as_slice();
                }

                (close_connection, current_offset) =
                    self.handle_raw_data(using_buffer, &mut frames).await?;

                if current_offset == 0 {
                    // no full dataframe, so safe in temp_buffer
                    if temp_buffer.is_empty() {
                        temp_buffer.append(&mut using_buffer.to_vec());
                    }
                    break;
                }
                offset += current_offset - last_offset;
                temp_buffer.clear();
            }
        }
        self.socket.shutdown().await?;
        Ok(())
    }
    fn handle_handshake(&mut self, buf: &[u8]) -> HttpHeader {
        let http_header = parse_http_header(buf.to_vec());
        if http_header.is_err() {
            return HttpHeader::response_400();
        }

        let http_header = http_header.unwrap();
        let websocket_key = http_header.fields.get("sec-websocket-key");

        if websocket_key.is_none() || websocket_key.unwrap().len() != 24 {
            return HttpHeader::response_400();
        }

        let mut request_key = websocket_key.unwrap().as_bytes().to_vec();

        request_key.append(&mut b"258EAFA5-E914-47DA-95CA-C5AB0DC85B11".to_vec());

        log::info!("Request Key: {}", String::from_utf8(request_key.clone()).unwrap());
        let response_key = sha1(request_key).to_vec();
        let response_key = base64::encode(&response_key);
        log::info!("After sha1: {response_key}");

        let mut response = HttpHeader::response_101();

        response
            .fields
            .insert("Sec-WebSocket-Accept".to_string(), response_key);

        self.request_header = Some(http_header);
        response
    }
}
