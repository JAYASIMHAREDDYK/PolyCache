#include "src/net/socket.hpp"
#include <iostream>
#include <cstring>

namespace polycache::net {

void initialize_network() {
#if defined(_WIN32) || defined(_WIN64)
    static bool initialized = false;
    if (!initialized) {
        WSADATA wsa_data;
        int res = WSAStartup(MAKEWORD(2, 2), &wsa_data);
        if (res != 0) {
            std::cerr << "[net] WSAStartup failed: " << res << std::endl;
        }
        initialized = true;
    }
#endif
}

void cleanup_network() {
#if defined(_WIN32) || defined(_WIN64)
    WSACleanup();
#endif
}

Socket::Socket() {
    initialize_network();
    handle_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
}

Socket::Socket(socket_handle_t handle) : handle_(handle) {
    initialize_network();
}

Socket::~Socket() {
    close();
}

Socket::Socket(Socket&& other) noexcept : handle_(other.handle_) {
    other.handle_ = INVALID_SOCK;
}

Socket& Socket::operator=(Socket&& other) noexcept {
    if (this != &other) {
        close();
        handle_ = other.handle_;
        other.handle_ = INVALID_SOCK;
    }
    return *this;
}

bool Socket::is_valid() const {
    return handle_ != INVALID_SOCK;
}

socket_handle_t Socket::handle() const {
    return handle_;
}

void Socket::close() {
    if (is_valid()) {
#if defined(_WIN32) || defined(_WIN64)
        ::closesocket(handle_);
#else
        ::close(handle_);
#endif
        handle_ = INVALID_SOCK;
    }
}

bool Socket::set_nonblocking(bool nonblocking) {
    if (!is_valid()) return false;
#if defined(_WIN32) || defined(_WIN64)
    u_long mode = nonblocking ? 1 : 0;
    return ::ioctlsocket(handle_, FIONBIO, &mode) == 0;
#else
    int flags = ::fcntl(handle_, F_GETFL, 0);
    if (flags < 0) return false;
    flags = nonblocking ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
    return ::fcntl(handle_, F_SETFL, flags) == 0;
#endif
}

bool Socket::set_reuseaddr(bool reuse) {
    if (!is_valid()) return false;
    int opt = reuse ? 1 : 0;
    return ::setsockopt(handle_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt)) == 0;
}

bool Socket::set_nodelay(bool nodelay) {
    if (!is_valid()) return false;
    int opt = nodelay ? 1 : 0;
    return ::setsockopt(handle_, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&opt), sizeof(opt)) == 0;
}

bool Socket::bind(const std::string& host, uint16_t port) {
    if (!is_valid()) return false;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (host.empty() || host == "0.0.0.0") {
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    } else {
        if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0) {
            return false;
        }
    }

    return ::bind(handle_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0;
}

bool Socket::listen(int backlog) {
    if (!is_valid()) return false;
    return ::listen(handle_, backlog) == 0;
}

Socket Socket::accept(std::string* client_ip, uint16_t* client_port) {
    if (!is_valid()) return Socket(INVALID_SOCK);

    sockaddr_in client_addr{};
    socklen_t addr_len = sizeof(client_addr);
    socket_handle_t client_fd = ::accept(handle_, reinterpret_cast<sockaddr*>(&client_addr), &addr_len);
    if (client_fd == INVALID_SOCK) {
        return Socket(INVALID_SOCK);
    }

    if (client_ip) {
        char ip_str[INET_ADDRSTRLEN];
        ::inet_ntop(AF_INET, &(client_addr.sin_addr), ip_str, INET_ADDRSTRLEN);
        *client_ip = ip_str;
    }
    if (client_port) {
        *client_port = ntohs(client_addr.sin_port);
    }

    return Socket(client_fd);
}

int64_t Socket::read(void* buffer, size_t max_bytes) {
    if (!is_valid()) return -2;

#if defined(_WIN32) || defined(_WIN64)
    int bytes = ::recv(handle_, reinterpret_cast<char*>(buffer), static_cast<int>(max_bytes), 0);
    if (bytes > 0) return bytes;
    if (bytes == 0) return 0; // EOF / closed
    int err = WSAGetLastError();
    if (err == WSAEWOULDBLOCK) return -1; // Would block
    return -2; // Error
#else
    ssize_t bytes = ::read(handle_, buffer, max_bytes);
    if (bytes > 0) return bytes;
    if (bytes == 0) return 0; // EOF / closed
    if (errno == EAGAIN || errno == EWOULDBLOCK) return -1;
    return -2; // Error
#endif
}

int64_t Socket::write(const void* buffer, size_t bytes) {
    if (!is_valid()) return -2;

#if defined(_WIN32) || defined(_WIN64)
    int sent = ::send(handle_, reinterpret_cast<const char*>(buffer), static_cast<int>(bytes), 0);
    if (sent >= 0) return sent;
    int err = WSAGetLastError();
    if (err == WSAEWOULDBLOCK) return -1;
    return -2;
#else
    ssize_t sent = ::write(handle_, buffer, bytes);
    if (sent >= 0) return sent;
    if (errno == EAGAIN || errno == EWOULDBLOCK) return -1;
    return -2;
#endif
}

} // namespace polycache::net
