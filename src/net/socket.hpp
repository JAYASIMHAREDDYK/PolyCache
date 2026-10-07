#pragma once

#include <string>
#include <cstdint>
#include <cstddef>

#if defined(_WIN32) || defined(_WIN64)
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
  using socket_handle_t = SOCKET;
  constexpr socket_handle_t INVALID_SOCK = INVALID_SOCKET;
#else
  #include <sys/types.h>
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <netinet/tcp.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  #include <fcntl.h>
  #include <errno.h>
  using socket_handle_t = int;
  constexpr socket_handle_t INVALID_SOCK = -1;
#endif

namespace polycache::net {

using socket_handle_t = ::socket_handle_t;
constexpr socket_handle_t INVALID_SOCK = ::INVALID_SOCK;

void initialize_network();
void cleanup_network();

class Socket {
public:
    Socket();
    explicit Socket(socket_handle_t handle);
    ~Socket();

    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    Socket(Socket&& other) noexcept;
    Socket& operator=(Socket&& other) noexcept;

    bool is_valid() const;
    socket_handle_t handle() const;
    void close();

    bool set_nonblocking(bool nonblocking = true);
    bool set_reuseaddr(bool reuse = true);
    bool set_nodelay(bool nodelay = true);

    bool bind(const std::string& host, uint16_t port);
    bool listen(int backlog = 1024);
    Socket accept(std::string* client_ip = nullptr, uint16_t* client_port = nullptr);

    // Returns > 0: bytes read/written, 0: connection closed, < 0: would block (-1) or error (-2)
    int64_t read(void* buffer, size_t max_bytes);
    int64_t write(const void* buffer, size_t bytes);

private:
    socket_handle_t handle_{INVALID_SOCK};
};

} // namespace polycache::net
