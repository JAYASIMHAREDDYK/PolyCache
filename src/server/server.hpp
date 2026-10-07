#pragma once

#include "src/net/socket.hpp"
#include "src/net/event_loop.hpp"
#include "src/server/client.hpp"
#include "src/storage/store.hpp"
#include "src/persistence/aof.hpp"
#include "src/metrics/metrics.hpp"
#include "src/command/command.hpp"
#include "src/protocol/resp.hpp"
#include <unordered_map>
#include <memory>
#include <string>
#include <chrono>

namespace polycache::server {

struct ServerConfig {
    std::string host{"0.0.0.0"};
    uint16_t port{6379};
    size_t maxmemory{64 * 1024 * 1024}; // Default 64MB
    eviction::EvictionType eviction_policy{eviction::EvictionType::AllKeysLRU};
    bool aof_enabled{true};
    std::string aof_filename{"appendonly.aof"};
    persistence::AofFsync aof_fsync{persistence::AofFsync::EverySec};
};

class Server {
public:
    explicit Server(ServerConfig config = {});
    ~Server();

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    bool start();
    void run();
    void stop();

    storage::Store& store() { return store_; }
    persistence::AofManager& aof() { return aof_; }
    metrics::MetricsRegistry& metrics() { return metrics_; }

private:
    void handle_accept(net::socket_handle_t fd, uint32_t events);
    void handle_client_event(net::socket_handle_t fd, uint32_t events);
    void process_client_input(Client& client);
    void try_flush_client_output(Client& client);
    void close_client(net::socket_handle_t fd);
    void server_cron();

    ServerConfig config_;
    net::Socket listener_;
    net::EventLoop loop_;
    storage::Store store_;
    persistence::AofManager aof_;
    metrics::MetricsRegistry metrics_;
    command::CommandDispatcher dispatcher_;
    protocol::RespParser parser_;

    std::unordered_map<net::socket_handle_t, std::unique_ptr<Client>> clients_;
    std::chrono::steady_clock::time_point last_cron_time_;
};

} // namespace polycache::server
