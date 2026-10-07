#pragma once

#include "src/net/socket.hpp"
#include <string>

namespace polycache::server {

struct Client {
    net::Socket socket;
    std::string ip;
    uint16_t port{0};

    std::string read_buffer;
    std::string write_buffer;
    bool closing{false};

    Client(net::Socket s, std::string ip_addr, uint16_t p)
        : socket(std::move(s)), ip(std::move(ip_addr)), port(p) {}
};

} // namespace polycache::server
