#include "src/persistence/aof.hpp"
#include <fcntl.h>
#include <sys/stat.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <filesystem>

#if defined(__linux__)
  #include <unistd.h>
  #include <sys/wait.h>
#elif defined(_WIN32) || defined(_WIN64)
  #include <io.h>
  #define fsync _commit
#endif

namespace polycache::persistence {

namespace {

void write_store_snapshot(storage::Store& store, const std::string& temp_filename) {
    std::ofstream out(temp_filename, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) return;

    store.raw_dict().for_each([&out](const std::string& key, const storage::ValueEntry& val) {
        if (val.is_expired()) return;

        if (val.type == storage::EntryType::String) {
            std::vector<std::string> set_cmd = {"SET", key, val.string_val};
            std::string serialized = protocol::RespSerializer::command(set_cmd);
            out.write(serialized.data(), serialized.size());

            if (val.expire_at_ms >= 0) {
                int64_t remaining_ms = val.expire_at_ms - storage::current_time_ms();
                if (remaining_ms > 0) {
                    std::vector<std::string> exp_cmd = {"EXPIRE", key, std::to_string((remaining_ms + 999) / 1000)};
                    std::string exp_ser = protocol::RespSerializer::command(exp_cmd);
                    out.write(exp_ser.data(), exp_ser.size());
                }
            }
        } else if (val.type == storage::EntryType::ZSet && val.zset_val) {
            auto items = val.zset_val->range_by_score(-1e308, 1e308);
            if (!items.empty()) {
                std::vector<std::string> zadd_cmd = {"ZADD", key};
                for (const auto& item : items) {
                    zadd_cmd.push_back(std::to_string(item.score));
                    zadd_cmd.push_back(item.member);
                }
                std::string serialized = protocol::RespSerializer::command(zadd_cmd);
                out.write(serialized.data(), serialized.size());
            }

            if (val.expire_at_ms >= 0) {
                int64_t remaining_ms = val.expire_at_ms - storage::current_time_ms();
                if (remaining_ms > 0) {
                    std::vector<std::string> exp_cmd = {"EXPIRE", key, std::to_string((remaining_ms + 999) / 1000)};
                    std::string exp_ser = protocol::RespSerializer::command(exp_cmd);
                    out.write(exp_ser.data(), exp_ser.size());
                }
            }
        }
    });

    out.flush();
}

} // namespace

AofManager::AofManager(std::string filename, AofFsync fsync_mode)
    : filename_(std::move(filename)), fsync_mode_(fsync_mode) {
    last_fsync_time_ = std::chrono::steady_clock::now();
    last_save_time_ = storage::current_time_sec();
}

AofManager::~AofManager() {
    close();
}

bool AofManager::open() {
    close();

#if defined(_WIN32) || defined(_WIN64)
    file_fd_ = ::_open(filename_.c_str(), _O_CREAT | _O_RDWR | _O_APPEND | _O_BINARY, _S_IREAD | _S_IWRITE);
#else
    file_fd_ = ::open(filename_.c_str(), O_CREAT | O_RDWR | O_APPEND | O_CLOEXEC, 0644);
#endif

    if (file_fd_ < 0) {
        return false;
    }

    try {
        current_size_ = std::filesystem::file_size(filename_);
    } catch (...) {
        current_size_ = 0;
    }

    return true;
}

void AofManager::close() {
    if (file_fd_ >= 0) {
        flush_and_sync();
#if defined(_WIN32) || defined(_WIN64)
        ::_close(file_fd_);
#else
        ::close(file_fd_);
#endif
        file_fd_ = -1;
    }
}

void AofManager::append_command(const std::vector<std::string>& args) {
    std::string serialized = protocol::RespSerializer::command(args);

    if (rewrite_in_progress_) {
        rewrite_buffer_.append(serialized);
    }

    if (file_fd_ >= 0) {
#if defined(_WIN32) || defined(_WIN64)
        ::_write(file_fd_, serialized.data(), static_cast<unsigned int>(serialized.size()));
#else
        ::write(file_fd_, serialized.data(), serialized.size());
#endif
        current_size_ += serialized.size();

        if (fsync_mode_ == AofFsync::Always) {
            fsync(file_fd_);
        }
    }
}

void AofManager::flush_and_sync() {
    if (file_fd_ >= 0) {
        fsync(file_fd_);
        last_fsync_time_ = std::chrono::steady_clock::now();
        last_save_time_ = storage::current_time_sec();
    }
}

bool AofManager::replay(storage::Store& store, size_t* out_commands_replayed) {
    std::ifstream in(filename_, std::ios::binary);
    if (!in.is_open()) {
        return true; // No AOF file to replay
    }

    std::stringstream ss;
    ss << in.rdbuf();
    std::string data = ss.str();
    in.close();

    protocol::RespParser parser;
    size_t offset = 0;
    size_t count = 0;

    while (offset < data.size()) {
        protocol::RespValue cmd_val;
        size_t consumed = 0;
        auto status = parser.parse(std::string_view(data.data() + offset, data.size() - offset), cmd_val, consumed);
        if (status != protocol::ParseStatus::Ok || consumed == 0) {
            break;
        }
        offset += consumed;

        if (cmd_val.type != protocol::RespType::Array || cmd_val.elements.empty()) {
            continue;
        }

        std::vector<std::string> args;
        for (const auto& elem : cmd_val.elements) {
            args.push_back(elem.str_val);
        }

        if (args.empty()) continue;
        std::string cmd = args[0];
        std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::toupper);

        if (cmd == "SET" && args.size() >= 3) {
            int64_t exp_ms = -1;
            if (args.size() >= 5) {
                std::string opt = args[3];
                std::transform(opt.begin(), opt.end(), opt.begin(), ::toupper);
                try {
                    int64_t val = std::stoll(args[4]);
                    if (opt == "EX") exp_ms = storage::current_time_ms() + val * 1000;
                    else if (opt == "PX") exp_ms = storage::current_time_ms() + val;
                } catch (...) {}
            }
            store.set(args[1], args[2], exp_ms);
        } else if (cmd == "DEL" && args.size() >= 2) {
            std::vector<std::string> keys(args.begin() + 1, args.end());
            store.del(keys);
        } else if (cmd == "INCR" && args.size() == 2) {
            int64_t out_val;
            std::string err;
            store.incr_by(args[1], 1, out_val, err);
        } else if (cmd == "DECR" && args.size() == 2) {
            int64_t out_val;
            std::string err;
            store.incr_by(args[1], -1, out_val, err);
        } else if (cmd == "EXPIRE" && args.size() == 3) {
            try {
                int64_t sec = std::stoll(args[2]);
                store.expire(args[1], storage::current_time_ms() + sec * 1000);
            } catch (...) {}
        } else if (cmd == "ZADD" && args.size() >= 4) {
            std::vector<std::pair<std::string, double>> members;
            for (size_t i = 2; i + 1 < args.size(); i += 2) {
                try {
                    double score = std::stod(args[i]);
                    members.emplace_back(args[i + 1], score);
                } catch (...) {}
            }
            int added = 0, updated = 0;
            std::string err;
            store.zadd(args[1], members, added, updated, err);
        } else if (cmd == "ZREM" && args.size() >= 3) {
            std::vector<std::string> members(args.begin() + 2, args.end());
            int removed = 0;
            std::string err;
            store.zrem(args[1], members, removed, err);
        } else if (cmd == "FLUSHDB") {
            store.flushdb();
        }

        count++;
    }

    if (out_commands_replayed) {
        *out_commands_replayed = count;
    }

    return true;
}

bool AofManager::start_bgrewrite(storage::Store& store, std::string& err) {
    if (rewrite_in_progress_) {
        err = "ERR Background append only file rewriting already in progress";
        return false;
    }

    rewrite_in_progress_ = true;
    rewrite_buffer_.clear();

    const std::string temp_filename = "temp-rewrite.aof";

#if defined(__linux__)
    pid_t pid = fork();
    if (pid < 0) {
        rewrite_in_progress_ = false;
        err = "ERR Can't fork background rewrite process";
        return false;
    }

    if (pid == 0) {
        // Child process: writes snapshot to temp-rewrite.aof and exits
        write_store_snapshot(store, temp_filename);
        _exit(0);
    }

    // Parent process
    rewrite_child_pid_ = pid;
    return true;
#else
    // Windows / non-Linux: background worker thread
    rewrite_thread_done_ = false;
    rewrite_thread_success_ = false;

    // Snapshot keys/values safely for worker thread
    rewrite_thread_ = std::make_unique<std::thread>([this, &store, temp_filename]() {
        try {
            write_store_snapshot(store, temp_filename);
            rewrite_thread_success_ = true;
        } catch (...) {
            rewrite_thread_success_ = false;
        }
        rewrite_thread_done_ = true;
    });

    return true;
#endif
}

void AofManager::periodic_tick(storage::Store& store) {
    // Check fsync every second
    if (fsync_mode_ == AofFsync::EverySec) {
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - last_fsync_time_).count() >= 1) {
            flush_and_sync();
        }
    }

    if (!rewrite_in_progress_) return;

#if defined(__linux__)
    int status = 0;
    pid_t res = waitpid(rewrite_child_pid_, &status, WNOHANG);
    if (res > 0) {
        bool success = WIFEXITED(status) && (WEXITSTATUS(status) == 0);
        finish_rewrite(store, success);
    }
#else
    if (rewrite_thread_done_) {
        if (rewrite_thread_ && rewrite_thread_->joinable()) {
            rewrite_thread_->join();
            rewrite_thread_.reset();
        }
        finish_rewrite(store, rewrite_thread_success_);
    }
#endif
}

void AofManager::finish_rewrite(storage::Store& /*store*/, bool success) {
    const std::string temp_filename = "temp-rewrite.aof";

    if (success) {
        // Append rewrite buffer to temp file
        if (!rewrite_buffer_.empty()) {
            std::ofstream out(temp_filename, std::ios::binary | std::ios::app);
            if (out.is_open()) {
                out.write(rewrite_buffer_.data(), rewrite_buffer_.size());
                out.flush();
            }
        }

        // Close old file handle
        close();

        // Atomically rename temp file to appendonly.aof
        std::error_code ec;
        std::filesystem::rename(temp_filename, filename_, ec);
        if (ec) {
            // On Windows rename may fail if target exists without remove
            std::filesystem::remove(filename_, ec);
            std::filesystem::rename(temp_filename, filename_, ec);
        }

        // Reopen new AOF
        open();
        last_save_time_ = storage::current_time_sec();
        last_bgrewrite_status_ = "ok";
    } else {
        last_bgrewrite_status_ = "err";
        std::error_code ec;
        std::filesystem::remove(temp_filename, ec);
    }

    rewrite_buffer_.clear();
    rewrite_in_progress_ = false;
}

} // namespace polycache::persistence
