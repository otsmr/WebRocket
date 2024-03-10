# Implementing WebSocket to learn

Sometimes I just want to learn a new language, and no, that has nothing to do
with [ThePrimeagen](https://twitter.com/ThePrimeagen) ([2022](https://twitter.com/intent/post?text=I%20know,%20Cpp,%20but%20let%20me%20cook,%20I%20just%20did%20not%20know%20@ThePrimeagen%20back%20then,%20so%20do%20not%20blame%20me%20for%20choosing%20Cpp%20in%202022...&url=https://github.com/otsmr/websocket),
[2023](https://twitter.com/ThePrimeagen/status/1634328728137265155),
[2024](https://twitter.com/ThePrimeagen/status/1761068465253744641)).


## Languages
### 2024 (current): Zig
```sh
cd zig/
zig build run
```
### 2023 - Rust
```sh
cd rust/ 
./cargo run
```
### 2022 - C++
```sh
cd cpp/
 ./build.sh run
```

## Getting started
1. Select lang and start server (see above)
2. Create test connection:
```sh
cat ./corpus/handshake - | nc localhost 8080
```


