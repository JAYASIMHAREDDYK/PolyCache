#include "src/server/server.hpp"
#include <iostream>
#include <chrono>

namespace polycache::server {

Server::Server(ServerConfig config)
    : config_(std::move(config)),
      store_(eviction::EvictionConfig{config_.maxmemory, config_.eviction_policy, 5}),
      aof_(config_.aof_filename, config_.aof_fsync) {
    last_cron_time_ = std::chrono::steady_clock::now();
}

Server::~Server() {
    stop();
}

bool Server::start() {
    if (config_.aof_enabled) {
        size_t replayed = 0;
        if (aof_.replay(store_, &replayed)) {
            if (replayed > 0) {
                std::cout << "[server] Successfully replayed " << replayed << " AOF commands\n";
            }
        }
        if (!aof_.open()) {
            std::cerr << "[server] Warning: Failed to open AOF file: " << config_.aof_filename << "\n";
        }
    }

    if (!listener_.set_reuseaddr(true)) {
        std::cerr << "[server] Warning: set_reuseaddr failed\n";
    }
    listener_.set_nodelay(true);

    if (!listener_.bind(config_.host, config_.port)) {
        std::cerr << "[server] Error: Failed to bind to " << config_.host << ":" << config_.port << "\n";
        return false;
    }

    if (!listener_.listen(1024)) {
        std::cerr << "[server] Error: Failed to listen\n";
        return false;
    }

    if (!listener_.set_nonblocking(true)) {
        std::cerr << "[server] Error: Failed to set non-blocking on listener\n";
        return false;
    }

    loop_.add_fd(listener_.handle(), net::EventType::Read, [this](net::socket_handle_t fd, uint32_t ev) {
        handle_accept(fd, ev);
    });

    std::cout << "[server] PolyCache listening on " << config_.host << ":" << config_.port << "\n";
    return true;
}

void Server::run() {
    while (loop_.is_running()) {
        loop_.run_once(20);
        server_cron();
    }
}

void Server::stop() {
    loop_.stop();
    for (auto& [fd, client] : clients_) {
        client->socket.close();
    }
    clients_.clear();
    listener_.close();
    if (config_.aof_enabled) {
        aof_.close();
    }
}

void Server::handle_accept(net::socket_handle_t, uint32_t) {
    while (true) {
        std::string client_ip;
        uint16_t client_port = 0;
        net::Socket client_sock = listener_.accept(&client_ip, &client_port);
        if (!client_sock.is_valid()) {
            break; // No more pending connections (EAGAIN/EWOULDBLOCK)
        }

        client_sock.set_nonblocking(true);
        client_sock.set_nodelay(true);

        net::socket_handle_t fd = client_sock.handle();
        auto client = std::make_unique<Client>(std::move(client_sock), std::move(client_ip), client_port);

        loop_.add_fd(fd, net::EventType::Read, [this](net::socket_handle_t client_fd, uint32_t ev) {
            handle_client_event(client_fd, ev);
        });

        clients_[fd] = std::move(client);
        metrics_.record_connection_opened();
    }
}

void Server::handle_client_event(net::socket_handle_t fd, uint32_t events) {
    auto it = clients_.find(fd);
    if (it == clients_.end()) return;
    Client& client = *(it->second);

    if (events & (net::EventType::Error | net::EventType::Hup)) {
        close_client(fd);
        return;
    }

    if (events & net::EventType::Read) {
        char buf[16384];
        while (true) {
            int64_t n = client.socket.read(buf, sizeof(buf));
            if (n > 0) {
                client.read_buffer.append(buf, static_cast<size_t>(n));
            } else if (n == -1) {
                // Would block (EAGAIN)
                break;
            } else {
                // EOF or error
                close_client(fd);
                return;
            }
        }
        process_client_input(client);
    }

    // Client could have been closed during process_client_input
    if (clients_.find(fd) == clients_.end()) return;

    if (events & net::EventType::Write) {
        try_flush_client_output(client);
    }
}

void Server::process_client_input(Client& client) {
    command::CommandContext ctx{store_, aof_, metrics_};

    while (!client.read_buffer.empty()) {
        protocol::RespValue cmd_val;
        size_t consumed = 0;
        auto status = parser_.parse(client.read_buffer, cmd_val, consumed);

        if (status == protocol::ParseStatus::Incomplete) {
            break;
        }

        if (status == protocol::ParseStatus::Error) {
            client.write_buffer.append(protocol::RespSerializer::error("Protocol error: unbalanced quotes or bad bulk length"));
            try_flush_client_output(client);
            close_client(client.socket.handle());
            return;
        }

        client.read_buffer.erase(0, consumed);

        std::vector<std::string> args;
        if (cmd_val.type == protocol::RespType::Array) {
            for (const auto& elem : cmd_val.elements) {
                args.push_back(elem.str_val);
            }
        } else if (cmd_val.type == protocol::RespType::BulkString || cmd_val.type == protocol::RespType::SimpleString) {
            args.push_back(cmd_val.str_val);
        }

        if (args.empty()) continue;

        auto t0 = std::chrono::high_resolution_clock::now();
        auto res = dispatcher_.execute(args, ctx);
        auto t1 = std::chrono::high_resolution_clock::now();
        uint64_t lat_us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();

        metrics_.record_command(lat_us);

        if (res.is_write && config_.aof_enabled) {
            aof_.append_command(args);
        }

        client.write_buffer.append(res.response);
    }

    try_flush_client_output(client);
}

void Server::try_flush_client_output(Client& client) {
    net::socket_handle_t fd = client.socket.handle();

    while (!client.write_buffer.empty()) {
        int64_t n = client.socket.write(client.write_buffer.data(), client.write_buffer.size());
        if (n > 0) {
            client.write_buffer.erase(0, static_cast<size_t>(n));
        } else if (n == -1) {
            // Socket buffer full, wait for EPOLLOUT
            loop_.modify_fd(fd, net::EventType::Read | net::EventType::Write);
            return;
        } else {
            // Error writing
            close_client(fd);
            return;
        }
    }

    // Fully drained output buffer, only listen for EPOLLIN
    loop_.modify_fd(fd, net::EventType::Read);
}

void Server::close_client(net::socket_handle_t fd) {
    loop_.remove_fd(fd);
    auto it = clients_.find(fd);
    if (it != clients_.end()) {
        it->second->socket.close();
        clients_.erase(it);
        metrics_.record_connection_closed();
    }
}

void Server::server_cron() {
    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_cron_time_).count() < 100) {
        return;
    }
    last_cron_time_ = now;

    // 1. Incremental rehash step (bounded)
    store_.step_rehash(100);

    // 2. Active expiration sampling cycle
    store_.active_expire_cycle(5);

    // 3. AOF periodic maintenance (fsync every sec check, child exit check)
    if (config_.aof_enabled) {
        aof_.periodic_tick(store_);
    }

    // 4. Update instantaneous metrics
    metrics_.tick_second();
}

} // namespace polycache::server
