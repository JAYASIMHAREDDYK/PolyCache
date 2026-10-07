#pragma once

#include "src/storage/store.hpp"
#include "src/protocol/resp.hpp"
#include <string>
#include <vector>
#include <cstdint>
#include <chrono>
#include <memory>
#include <thread>
#include <atomic>

namespace polycache::persistence {

enum class AofFsync {
    Always,
    EverySec,
    No
};

class AofManager {
public:
    AofManager(std::string filename = "appendonly.aof",
               AofFsync fsync_mode = AofFsync::EverySec);
    ~AofManager();

    AofManager(const AofManager&) = delete;
    AofManager& operator=(const AofManager&) = delete;

    bool open();
    void close();

    // Appends a mutating command to AOF
    void append_command(const std::vector<std::string>& args);

    // Replays existing AOF onto store during startup/recovery
    bool replay(storage::Store& store, size_t* out_commands_replayed = nullptr);

    // Triggers BGREWRITEAOF
    bool start_bgrewrite(storage::Store& store, std::string& err);

    // Called periodically by event loop (e.g. every 100ms)
    void periodic_tick(storage::Store& store);

    bool is_rewrite_in_progress() const { return rewrite_in_progress_; }
    size_t current_size() const { return current_size_; }
    uint64_t last_save_time() const { return last_save_time_; }
    std::string last_bgrewrite_status() const { return last_bgrewrite_status_; }

    void set_fsync_mode(AofFsync mode) { fsync_mode_ = mode; }
    AofFsync fsync_mode() const { return fsync_mode_; }

private:
    void flush_and_sync();
    void finish_rewrite(storage::Store& store, bool success);

    std::string filename_;
    AofFsync fsync_mode_{AofFsync::EverySec};
    int file_fd_{-1};
    size_t current_size_{0};
    uint64_t last_save_time_{0};
    std::string last_bgrewrite_status_{"ok"};

    std::string buffer_;
    std::string rewrite_buffer_;
    std::atomic<bool> rewrite_in_progress_{false};
    std::chrono::steady_clock::time_point last_fsync_time_;

#if defined(__linux__)
    pid_t rewrite_child_pid_{-1};
#else
    std::unique_ptr<std::thread> rewrite_thread_;
    std::atomic<bool> rewrite_thread_done_{false};
    std::atomic<bool> rewrite_thread_success_{false};
#endif
};

} // namespace polycache::persistence
