#include "src/server/server.hpp"
#include <iostream>
#include <csignal>
#include <string>
#include <cstring>

namespace {
polycache::server::Server* g_server = nullptr;

void signal_handler(int sig) {
    std::cout << "\n[server] Received signal " << sig << ", shutting down cleanly...\n";
    if (g_server) {
        g_server->stop();
    }
}

size_t parse_memory_str(const std::string& str) {
    if (str.empty()) return 0;
    size_t multiplier = 1;
    std::string num_part = str;
    char unit = std::toupper(str.back());
    if (unit == 'B' && str.size() > 1) {
        char prev = std::toupper(str[str.size() - 2]);
        if (prev == 'K') { multiplier = 1024; num_part = str.substr(0, str.size() - 2); }
        else if (prev == 'M') { multiplier = 1024 * 1024; num_part = str.substr(0, str.size() - 2); }
        else if (prev == 'G') { multiplier = 1024ULL * 1024 * 1024; num_part = str.substr(0, str.size() - 2); }
        else { num_part = str.substr(0, str.size() - 1); }
    } else if (unit == 'K') {
        multiplier = 1024;
        num_part = str.substr(0, str.size() - 1);
    } else if (unit == 'M') {
        multiplier = 1024 * 1024;
        num_part = str.substr(0, str.size() - 1);
    } else if (unit == 'G') {
        multiplier = 1024ULL * 1024 * 1024;
        num_part = str.substr(0, str.size() - 1);
    }

    try {
        return std::stoull(num_part) * multiplier;
    } catch (...) {
        return 64 * 1024 * 1024;
    }
}

void print_help(const char* prog) {
    std::cout << "PolyCache - High-Performance In-Memory Key-Value Datastore\n\n"
              << "Usage: " << prog << " [options]\n\n"
              << "Options:\n"
              << "  --port <port>         TCP port to listen on (default: 6379)\n"
              << "  --host <ip>           Host IP to bind to (default: 0.0.0.0)\n"
              << "  --maxmemory <size>    Max memory limit e.g. 64mb, 2gb (default: 64mb)\n"
              << "  --policy <policy>     Eviction policy: allkeys-lru, allkeys-lfu, noeviction (default: allkeys-lru)\n"
              << "  --aof <yes|no>        Enable AOF persistence (default: yes)\n"
              << "  --fsync <mode>        AOF fsync mode: always, everysec, no (default: everysec)\n"
              << "  --aof-file <filename> Path to AOF log file (default: appendonly.aof)\n"
              << "  --help                Show this help message\n";
}

} // namespace

int main(int argc, char* argv[]) {
    polycache::server::ServerConfig config;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            print_help(argv[0]);
            return 0;
        } else if (arg == "--port" && i + 1 < argc) {
            config.port = static_cast<uint16_t>(std::stoi(argv[++i]));
        } else if (arg == "--host" && i + 1 < argc) {
            config.host = argv[++i];
        } else if (arg == "--maxmemory" && i + 1 < argc) {
            config.maxmemory = parse_memory_str(argv[++i]);
        } else if (arg == "--policy" && i + 1 < argc) {
            config.eviction_policy = polycache::eviction::parse_policy(argv[++i]);
        } else if (arg == "--aof" && i + 1 < argc) {
            std::string val = argv[++i];
            config.aof_enabled = (val == "yes" || val == "true" || val == "1");
        } else if (arg == "--aof-file" && i + 1 < argc) {
            config.aof_filename = argv[++i];
        } else if (arg == "--fsync" && i + 1 < argc) {
            std::string mode = argv[++i];
            if (mode == "always") config.aof_fsync = polycache::persistence::AofFsync::Always;
            else if (mode == "no") config.aof_fsync = polycache::persistence::AofFsync::No;
            else config.aof_fsync = polycache::persistence::AofFsync::EverySec;
        }
    }

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    polycache::server::Server server(config);
    g_server = &server;

    if (!server.start()) {
        std::cerr << "[server] Fatal: Server failed to start.\n";
        return 1;
    }

    std::cout << "[server] Server initialized with maxmemory=" << (config.maxmemory / (1024 * 1024))
              << "MB, policy=" << polycache::eviction::policy_to_string(config.eviction_policy)
              << ", AOF=" << (config.aof_enabled ? "enabled" : "disabled") << "\n";

    server.run();

    std::cout << "[server] PolyCache stopped cleanly.\n";
    return 0;
}
