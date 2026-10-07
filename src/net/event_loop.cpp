#include "src/net/event_loop.hpp"
#include <iostream>
#include <algorithm>

namespace polycache::net {

#if defined(__linux__)

EventLoop::EventLoop() {
    epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd_ < 0) {
        std::cerr << "[net] Failed to create epoll instance\n";
    }
    epoll_events_.resize(1024);
}

EventLoop::~EventLoop() {
    if (epoll_fd_ >= 0) {
        ::close(epoll_fd_);
    }
}

bool EventLoop::add_fd(socket_handle_t fd, uint32_t events, EventCallback callback) {
    if (epoll_fd_ < 0 || fd < 0) return false;

    struct epoll_event ev{};
    ev.data.fd = fd;
    ev.events = EPOLLET; // Edge-triggered epoll
    if (events & EventType::Read) ev.events |= EPOLLIN | EPOLLRDHUP;
    if (events & EventType::Write) ev.events |= EPOLLOUT;

    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) < 0) {
        return false;
    }

    handlers_[fd] = {fd, events, std::move(callback)};
    return true;
}

bool EventLoop::modify_fd(socket_handle_t fd, uint32_t events) {
    if (epoll_fd_ < 0 || fd < 0) return false;
    auto it = handlers_.find(fd);
    if (it == handlers_.end()) return false;

    struct epoll_event ev{};
    ev.data.fd = fd;
    ev.events = EPOLLET;
    if (events & EventType::Read) ev.events |= EPOLLIN | EPOLLRDHUP;
    if (events & EventType::Write) ev.events |= EPOLLOUT;

    if (epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev) < 0) {
        return false;
    }

    it->second.events = events;
    return true;
}

bool EventLoop::remove_fd(socket_handle_t fd) {
    if (epoll_fd_ < 0 || fd < 0) return false;
    handlers_.erase(fd);
    epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
    return true;
}

int EventLoop::run_once(int timeout_ms) {
    if (epoll_fd_ < 0 || !running_) return 0;

    int num_events = epoll_wait(epoll_fd_, epoll_events_.data(), static_cast<int>(epoll_events_.size()), timeout_ms);
    if (num_events < 0) {
        if (errno == EINTR) return 0;
        return -1;
    }

    for (int i = 0; i < num_events; ++i) {
        int fd = epoll_events_[i].data.fd;
        uint32_t ev = epoll_events_[i].events;

        auto it = handlers_.find(fd);
        if (it == handlers_.end()) continue;

        uint32_t dispatched = 0;
        if (ev & (EPOLLIN | EPOLLPRI)) dispatched |= EventType::Read;
        if (ev & EPOLLOUT) dispatched |= EventType::Write;
        if (ev & EPOLLERR) dispatched |= EventType::Error;
        if (ev & (EPOLLHUP | EPOLLRDHUP)) dispatched |= EventType::Hup;

        if (dispatched && it->second.callback) {
            it->second.callback(fd, dispatched);
        }
    }

    if (static_cast<size_t>(num_events) == epoll_events_.size()) {
        epoll_events_.resize(epoll_events_.size() * 2);
    }

    return num_events;
}

#elif defined(_WIN32) || defined(_WIN64)

EventLoop::EventLoop() {
    initialize_network();
}

EventLoop::~EventLoop() = default;

bool EventLoop::add_fd(socket_handle_t fd, uint32_t events, EventCallback callback) {
    if (fd == INVALID_SOCK) return false;
    handlers_[fd] = {fd, events, std::move(callback)};
    return true;
}

bool EventLoop::modify_fd(socket_handle_t fd, uint32_t events) {
    auto it = handlers_.find(fd);
    if (it == handlers_.end()) return false;
    it->second.events = events;
    return true;
}

bool EventLoop::remove_fd(socket_handle_t fd) {
    handlers_.erase(fd);
    return true;
}

int EventLoop::run_once(int timeout_ms) {
    if (!running_ || handlers_.empty()) {
        if (timeout_ms > 0) {
            Sleep(timeout_ms);
        }
        return 0;
    }

    poll_fds_.clear();
    poll_fds_.reserve(handlers_.size());

    for (const auto& [fd, h] : handlers_) {
        WSAPOLLFD pfd{};
        pfd.fd = fd;
        pfd.events = 0;
        if (h.events & EventType::Read) pfd.events |= POLLRDNORM;
        if (h.events & EventType::Write) pfd.events |= POLLWRNORM;
        pfd.revents = 0;
        poll_fds_.push_back(pfd);
    }

    int ret = WSAPoll(poll_fds_.data(), static_cast<ULONG>(poll_fds_.size()), timeout_ms);
    if (ret <= 0) return ret;

    // Snapshot ready list in case callbacks modify handlers_
    std::vector<std::pair<socket_handle_t, uint32_t>> ready;
    for (const auto& pfd : poll_fds_) {
        if (pfd.revents == 0) continue;
        uint32_t dispatched = 0;
        if (pfd.revents & (POLLRDNORM | POLLRDBAND)) dispatched |= EventType::Read;
        if (pfd.revents & POLLWRNORM) dispatched |= EventType::Write;
        if (pfd.revents & POLLERR) dispatched |= EventType::Error;
        if (pfd.revents & POLLHUP) dispatched |= EventType::Hup;

        if (dispatched) {
            ready.emplace_back(pfd.fd, dispatched);
        }
    }

    int fired = 0;
    for (const auto& [fd, events] : ready) {
        auto it = handlers_.find(fd);
        if (it != handlers_.end() && it->second.callback) {
            it->second.callback(fd, events);
            fired++;
        }
    }

    return fired;
}

#endif

void EventLoop::stop() {
    running_ = false;
}

} // namespace polycache::net
