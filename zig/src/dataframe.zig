//
// Copyright (c) 2024, Tobias Müller <git@tsmr.eu>
//

const std = @import("std");
const ws = @import("websocket.zig");

const Opcode = enum(usize) {
    ContinuationFrame = 0x0,
    TextFrame = 0x1,
    BinaryFrame = 0x2,
    ConectionClose = 0x8,
    Ping = 0x9,
    Pong = 0xA,
};

const DataFrameFlags = struct {
    fin: bool = true,
    rsv1: bool = false,
    rsv2: bool = false,
    rsv3: bool = false,
    mask: bool = false,

    fn to_bytes(self: *DataFrameFlags) [2]u8 {
        var ret: [2]u8 = .{ 0, 0 };
        if (self.mask) {
            ret[1] |= 1 << 7;
        }
        if (self.fin) {
            ret[0] |= 1 << 7;
        }
        if (self.rsv1) {
            ret[0] |= 1 << 6;
        }
        if (self.rsv2) {
            ret[0] |= 1 << 5;
        }
        if (self.rsv3) {
            ret[0] |= 1 << 4;
        }
        return ret;
    }
    fn from_bytes(b1: u8, b2: u8) DataFrameFlags {
        return DataFrameFlags{
            .fin = (b1 & (1 << 7)) > 0,
            .rsv1 = b1 & (1 << 6) > 0,
            .rsv2 = b1 & (1 << 5) > 0,
            .rsv3 = b1 & (1 << 4) > 0,
            .mask = b2 & 0x80 > 0,
        };
    }
};

pub const Dataframe = struct {
    opcode: Opcode,
    flags: DataFrameFlags = DataFrameFlags{},
    masking_key: ?[4]u8 = null,
    payload_filled_len: u64 = 0,
    // Will be allocated with the payload_len so the payload.len will be
    // exactly the size of the dataframe payload len
    payload: []u8 = undefined,
    consumed_len: u64 = 0,
    allocator: std.mem.Allocator,
    raw_bytes: ?[]u8 = null,

    pub fn get_pong(allocator: std.mem.Allocator, payload: []u8) !Dataframe {
        return Dataframe{ .opcode = .Pong, .payload = payload, .payload_filled_len = payload.len, .allocator = allocator };
    }

    pub fn get_ping(allocator: std.mem.Allocator) !Dataframe {
        var payload = try allocator.alloc(u8, 0);
        return Dataframe{ .opcode = .Ping, .payload = payload, .payload_filled_len = payload.len, .allocator = allocator };
    }

    pub fn closing(allocator: std.mem.Allocator, statuscode: u16) !Dataframe {
        var payload = try allocator.alloc(u8, 2);
        payload[0] = @intCast(statuscode >> 8);
        payload[1] = @intCast(statuscode & 0xff);
        return Dataframe{ .opcode = .ConectionClose, .payload = payload, .payload_filled_len = payload.len, .allocator = allocator };
    }

    pub fn from_websocket_data(allocator: std.mem.Allocator, data: ws.WebSocketData) !Dataframe {
        var opcode: Opcode = undefined;
        switch (data.type) {
            .Text => opcode = Opcode.TextFrame,
            .Binary => opcode = Opcode.BinaryFrame,
        }
        var flags = DataFrameFlags{};
        return Dataframe{ .opcode = opcode, .flags = flags, .payload = data.payload, .payload_filled_len = data.payload.len, .allocator = allocator };
    }

    pub fn to_raw_bytes(self: *Dataframe) ![]u8 {
        var buf: []u8 = undefined;
        if (self.raw_bytes != null and buf.len >= self.payload.len + 100) {
            buf = self.raw_bytes.?;
        } else {
            buf = try self.allocator.alloc(u8, self.payload.len + 100);
            self.raw_bytes = buf;
        }

        var flags = self.flags.to_bytes();

        buf[0] = @intCast(@intFromEnum(self.opcode));
        buf[0] |= flags[0];
        buf[1] = flags[1];

        var header_size: usize = 2;

        if (self.payload.len > 0xFFFF) {
            buf[1] |= 127;
            for (0..8) |i| {
                buf[header_size + i] = @intCast((self.payload.len >> @intCast(((7 - i) * 8)) & 0xff));
            }
            header_size += 8;
        } else if (self.payload.len >= 126) {
            buf[1] |= 126;
            buf[header_size] = @intCast(self.payload.len >> 8 & 0xff);
            buf[header_size + 1] = @intCast(self.payload.len & 0xff);
            header_size += 2;
        } else {
            buf[1] |= @intCast(self.payload.len);
        }
        std.mem.copy(u8, buf[header_size..], self.payload);
        return buf[0 .. header_size + self.payload.len];
    }

    pub fn get_closing_code(self: *Dataframe) !u16 {
        if (self.payload.len == 0) {
            return 1000;
        }
        if (self.payload.len < 2) {
            return error.ProtocolError;
        }
        var statuscode: u16 = (@as(u16, @intCast(self.payload[0])) << 8) | @as(u16, @intCast(self.payload[1]));
        return statuscode;
    }

    pub fn deinit(self: *Dataframe) void {
        self.allocator.free(self.payload);
        if (self.raw_bytes != null) {
            self.allocator.free(self.raw_bytes.?);
        }
    }

    pub fn is_fully_received(self: *Dataframe) bool {
        return self.get_missing_payload_size() == 0;
    }
    pub fn get_missing_payload_size(self: *Dataframe) usize {
        return self.payload.len - self.payload_filled_len;
    }

    pub fn add_payload(self: *Dataframe, data: []u8) !void {
        if (self.payload.len < self.payload_filled_len + data.len) {
            return error.ToMuchData;
        }
        if (self.flags.mask) {
            if (self.masking_key) |masking_key| {
                for (0..data.len) |i| {
                    const index = self.payload_filled_len + i;
                    self.payload[index] = data[i] ^ masking_key[index % 4];
                }
            }
        } else {
            std.mem.copy(u8, self.payload[self.payload_filled_len..], data);
        }
        self.payload_filled_len += data.len;
    }

    pub fn parse(allocator: std.mem.Allocator, data: []const u8) !Dataframe {
        //  0               1               2               3
        //  0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7
        // +-+-+-+-+-------+-+-------------+-------------------------------+
        // |F|R|R|R| opcode|M| Payload len |    Extended payload length    |
        // |I|S|S|S|  (4)  |A|     (7)     |             (16/64)           |
        // |N|V|V|V|       |S|             |   (if payload len==126/127)   |
        // | |1|2|3|       |K|             |                               |
        // +-+-+-+-+-------+-+-------------+ - - - - - - - - - - - - - - - +
        // |     Extended payload length continued, if payload len == 127  |
        // + - - - - - - - - - - - - - - - +-------------------------------+
        // |                               |Masking-key, if MASK set to 1  |
        // +-------------------------------+-------------------------------+
        // | Masking-key (continued)       |          Payload Data         |
        // +-------------------------------- - - - - - - - - - - - - - - - +
        // :                     Payload Data continued ...                :
        // + - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - +
        // |                     Payload Data continued ...                |
        // +---------------------------------------------------------------+

        var header_size: usize = 2;
        if (data.len < header_size) {
            return error.NoHeaderFound;
        }

        var opcode: Opcode = try std.meta.intToEnum(Opcode, data[0] & 0b1111);
        var flags: DataFrameFlags = DataFrameFlags.from_bytes(data[0], data[1]);

        if (flags.rsv1 or flags.rsv2 or flags.rsv3) {
            return error.ReservedFlagsSet;
        }

        var payload_size: u64 = (data[1] & 0x7F);
        if (payload_size == 126) {
            header_size = 4;
            if (data.len < header_size) {
                return error.HeaderToShort;
            }
            payload_size = @as(u64, data[2]) << 8;
            payload_size += data[3];
        } else if (payload_size == 127) {
            // if data[0] >> 7 == 1 {
            //     data[0] &= 0x7F;
            // }

            header_size = 10;
            if (data.len < header_size) {
                return error.HeaderToShort;
            }
            payload_size = @as(u64, data[2]) << 56;
            payload_size += @as(u64, data[3]) << 48;
            payload_size += @as(u64, data[4]) << 40;
            payload_size += @as(u64, data[5]) << 32;
            payload_size += @as(u64, data[6]) << 24;
            payload_size += @as(u64, data[7]) << 16;
            payload_size += @as(u64, data[8]) << 8;
            payload_size += @as(u64, data[9]);
        }

        var masking_key: [4]u8 = [_]u8{0} ** 4;

        if (flags.mask) {
            if (data.len < header_size + 4) {
                return error.HeaderToShort;
            }
            masking_key[0] = data[header_size];
            masking_key[1] = data[header_size + 1];
            masking_key[2] = data[header_size + 2];
            masking_key[3] = data[header_size + 3];
            header_size += 4;
        }

        var payload = try allocator.alloc(u8, payload_size);
        var payload_filled_len: u64 = 0;

        if (payload_size > 0) {
            var remaining = payload_size + header_size;
            if (remaining > data.len) {
                remaining = data.len;
            }
            payload_filled_len = remaining - header_size;
            std.mem.copy(u8, payload, data[header_size..remaining]);
            if (flags.mask) {
                for (0..payload_filled_len) |i| {
                    payload[i] ^= masking_key[i % 4];
                }
            }
        }

        return Dataframe{ .opcode = opcode, .masking_key = masking_key, .payload = payload, .payload_filled_len = payload_filled_len, .flags = flags, .consumed_len = payload_filled_len + header_size, .allocator = allocator };
    }
};
