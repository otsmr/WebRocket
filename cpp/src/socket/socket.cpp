/*
 * Copyright (c) 2022, Tobias <git@tsmr.eu>
 * 
 */

#include "socket.h"

Socket::~Socket() = default;
Socket::Socket(int port) {
    m_port = port;
}
Socket::Socket(int port, bool use_tls, int max_connections) {
    m_max_connections = max_connections;
    m_port = port;
    m_use_tls = use_tls;
}

void Socket::stop() {

#if !COMPILE_FOR_FUZZING

    m_state = Socket::Stopping;

    int sockfd = socket(AF_INET, SOCK_STREAM, 0); 

    sockaddr_in sockaddr{};
    sockaddr.sin_family = AF_INET;
    sockaddr.sin_addr.s_addr = INADDR_ANY;
    sockaddr.sin_port = htons(m_port); 

    int close_thread = connect(sockfd, (struct sockaddr*)& sockaddr, sizeof(sockaddr));

    while (m_state != State::Stopped)
        sleep(1);

    close(close_thread);
    close(m_sockfd);

#endif

}

void Socket::wait_for_connection () {

    auto addrlen = sizeof(m_sockaddr);

#if COMPILE_FOR_FUZZING
    for (int i = 0; i < 1; i++) {
        printf("open %s\n", g_fuzzing_input_file);
        auto connection = open(g_fuzzing_input_file, O_RDONLY);
#else
    while (true) {
        auto connection = accept(m_sockfd, (struct sockaddr*)&m_sockaddr, (socklen_t*)&addrlen);
#endif 
        if (connection < 0) {
            std::cout << "Failed to grab connection. errno: " << errno << std::endl;
            return;
        }

        if (m_state != Socket::Running) {
            // TODO: close websockets?
            m_state = State::Stopped;
            return;
        }

        if (m_max_connections <= m_current_connections) {
            std::cout << "Maximum number of connections reached.\n";
            close(connection);
            continue;
        }

        auto webSocketConnection = [&]() {

            m_current_connections++;

            WebSocket webSocket(connection);

            if (m_on_open != nullptr)
                m_on_open(&webSocket);

            webSocket.listen();

            m_current_connections--;

        };

#if USEFORK
        std::thread([&](){
#endif
            if (m_use_tls) {
                std::cout << "TLS Handshake, ..." << std::endl;
                // TLSWrapper tlsWrapper;
                // tlsWrapper.listen_to_socket(socket, [&]);
            } else {
                webSocketConnection();
            }
#if USEFORK
        }).detach();
#endif

    }

}

#if USEFORK
bool Socket::listen (bool async) {
#else
bool Socket::listen () {
#endif

#if !COMPILE_FOR_FUZZING

    // TODO: AF_INET6 -> own thread?
    m_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (m_sockfd == -1) {
        std::cout << "Failed to create socket. errno: " << errno << std::endl;
        return false;
    } 

    m_sockaddr.sin_family = AF_INET;
    m_sockaddr.sin_addr.s_addr = INADDR_ANY;
    m_sockaddr.sin_port = htons(m_port); 
    
    if (bind(m_sockfd, (struct sockaddr*)&m_sockaddr, sizeof(m_sockaddr)) < 0) {
        std::cout << "Failed to bind to port " << m_port << ". errno: " << errno << std::endl;
        return false;
    }

    if (::listen(m_sockfd, m_max_connections) < 0) {
        std::cout << "Failed to listen on socket. errno: " << errno << std::endl;
        return false;
    }

#endif 

#if USEFORK
    if (async) {
        std::thread ([&]() {
            wait_for_connection();
        }).detach();
    } else {
        wait_for_connection();
    }
#else
    wait_for_connection();
#endif

    return true;

}