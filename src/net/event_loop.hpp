#pragma once

#include "src/net/socket.hpp"
#include <functional>
#include <vector>
#include <unordered_map>
#include <cstdint>

#if defined(__linux__)
  #include <sys/epoll.h>
#endif

namespace polycache::net {

namespace EventType {
    constexpr uint32_t Read  = 1 << 0;
    constexpr uint32_t Write = 1 << 1;
    constexpr uint32_t Error = 1 << 2;
    constexpr uint32_t Hup   = 1 << 3;
}

using EventCallback = std::function<void(socket_handle_t, uint32_t)>;

class EventLoop {
public:
    EventLoop();
    ~EventLoop();

    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;

    bool add_fd(socket_handle_t fd, uint32_t events, EventCallback callback);
    bool modify_fd(socket_handle_t fd, uint32_t events);
    bool remove_fd(socket_handle_t fd);

    // Runs a single iteration of polling. Returns number of events dispatched.
    int run_once(int timeout_ms = 100);

    void stop();
    bool is_running() const { return running_; }

private:
    struct Handler {
        socket_handle_t fd;
        uint32_t events;
        EventCallback callback;
    };

    bool running_{true};
    std::unordered_map<socket_handle_t, Handler> handlers_;

#if defined(__linux__)
    int epoll_fd_{-1};
    std::vector<struct epoll_event> epoll_events_;
#elif defined(_WIN32) || defined(_WIN64)
    std::vector<WSAPOLLFD> poll_fds_;
#endif
};

} // namespace polycache::net
