//
// Copyright (c) 2024, Tobias Müller <git@tsmr.eu>
//

const std = @import("std");
const Dataframe = @import("dataframe.zig").Dataframe;
const ws = @import("websocket.zig");

const Context = struct {};

pub fn main() !void {
    var general_purpose_allocator = std.heap.GeneralPurposeAllocator(.{}){};
    defer _ = general_purpose_allocator.detectLeaks();

    const allocator = general_purpose_allocator.allocator();

    std.log.info("Starting WebSocket Server", .{});

    var ctx = Context{};

    try ws.listen(Handler, allocator, &ctx, .{});
}

const Handler = struct {
    conn: *ws.WebSocketConnection,
    context: *Context,

    pub fn init(conn: *ws.WebSocketConnection, context: *Context) !Handler {
        return .{ .conn = conn, .context = context };
    }

    pub fn handle(self: *Handler, data: ws.WebSocketData) !void {
        _ = self;
        var pbuf = data.payload;
        if (pbuf.len > 15) {
            pbuf = data.payload[0..15];
        }
        std.log.info("New message: {s}", .{pbuf});
    }
};

// Running all tests with zig build test, is there a better solution?
comptime {
    _ = @import("base64.zig");
}
