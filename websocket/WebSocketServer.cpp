/*
 * Copyright (c) 2022, Tobias <git@tsmr.eu>
 * 
 */

#include <iostream>
#include <string>
#include "base64/base64.h"
#include "sha1/sha1.h"
#include "http/HttpRequest.h"

int main () {

    std::string header = 
        "GET /chat HTTP/1.1\n" \
        "Host: server.example.com\n" \
        "Upgrade: websocket\n" \
        "Connection: Upgrade\n" \
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\n" \
        "Origin: http://example.com\n" \
        "Sec-WebSocket-Protocol: chat, superchat\n" \
        "Sec-WebSocket-Version: 13";

    HTTP::HttpRequest Request;

    std::vector<uint8_t> raw_header(header.begin(), header.end());
    raw_header.assign(header.begin(), header.end());

    Request.from_raw_request(raw_header);

    std::vector<HTTP::HttpRequest::Header> headers = Request.headers();

    for (const HTTP::HttpRequest::Header& header : headers) 
        std::cout << "\n" << header.name << ": " << header.value;

    std::string sec_key = 
        Request.get_header("sec-websocket-key").value +
        "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

    uint8_t hash[20];
    sha1((uint8_t *) sec_key.c_str(), hash, sec_key.size());

    char * output = NULL;
    base64_encode(hash, &output, 20);

    printf("\nIST:  %s\n", output);
    printf("SOLL: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\n");

    return 0;

}