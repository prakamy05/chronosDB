#pragma once
#include <iostream>
#include <thread>
#include <string>
#include <vector>
#include <functional>

#ifdef _WIN32
    #include <winsock2.h>
    #pragma comment(lib, "ws2_32.lib")
    using socket_t = SOCKET;
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <unistd.h>
    using socket_t = int;
    constexpr socket_t INVALID_SOCKET = -1;
    constexpr int SOCKET_ERROR = -1;
#endif

class TCPServer {
private:
    socket_t server_fd{INVALID_SOCKET};
    int port;
    bool running{false};
    std::thread listener_thread;
    std::function<std::string(std::string)> query_handler;

    void ListenLoop() {
        while (running) {
            struct sockaddr_in client_addr;
            unsigned int addr_len = sizeof(client_addr);
            socket_t client_fd = accept(server_fd, (struct sockaddr*)&client_addr, (int*)&addr_len);
            if (client_fd == INVALID_SOCKET) continue;

            std::thread([this, client_fd]() {
                char buffer[1024] = {0};
#ifdef _WIN32
                int bytes_read = recv(client_fd, buffer, 1024, 0);
#else
                int bytes_read = read(client_fd, buffer, 1024);
#endif
                if (bytes_read > 0) {
                    std::string sql(buffer);
                    std::string result = query_handler(sql);
                    send(client_fd, result.c_str(), result.size(), 0);
                }
#ifdef _WIN32
                closesocket(client_fd);
#else
                close(client_fd);
#endif
            }).detach();
        }
    }

public:
    TCPServer(int p, std::function<std::string(std::string)> handler) 
        : port(p), query_handler(handler) {}

    void Start() {
#ifdef _WIN32
        WSADATA wsa;
        WSAStartup(MAKEWORD(2,2), &wsa);
#endif
        server_fd = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in address;
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(port);

        int opt = 1;
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));

        if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) == SOCKET_ERROR) {
            std::cerr << "[Network Core] Failed binding client server port.\n";
            return;
        }
        listen(server_fd, 10);
        running = true;
        listener_thread = std::thread(&TCPServer::ListenLoop, this);
        std::cout << "[Network Core] Server engine boundary bound listening on port " << port << "\n";
    }

    void Stop() {
        running = false;
#ifdef _WIN32
        closesocket(server_fd);
        WSACleanup();
#else
        close(server_fd);
#endif
        if (listener_thread.joinable()) listener_thread.join();
    }
};