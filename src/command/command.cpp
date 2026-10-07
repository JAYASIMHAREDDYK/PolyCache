#include "src/command/command.hpp"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cmath>

namespace polycache::command {

namespace {

std::string to_upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::toupper);
    return s;
}

bool parse_score_bound(std::string_view s, double& out_score, bool& out_exclusive) {
    if (s.empty()) return false;
    out_exclusive = false;
    if (s[0] == '(') {
        out_exclusive = true;
        s.remove_prefix(1);
    }
    std::string lower(s);
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    if (lower == "-inf") {
        out_score = -1e308;
        return true;
    }
    if (lower == "+inf" || lower == "inf") {
        out_score = 1e308;
        return true;
    }
    try {
        out_score = std::stod(std::string(s));
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace

CommandDispatcher::CommandDispatcher() {
    // --- PING ---
    register_command("PING", [](const std::vector<std::string>& args, CommandContext&) -> ExecutionResult {
        if (args.size() > 2) {
            return {protocol::RespSerializer::error("wrong number of arguments for 'ping' command"), false, false};
        }
        if (args.size() == 2) {
            return {protocol::RespSerializer::pong(args[1]), false, true};
        }
        return {protocol::RespSerializer::pong(), false, true};
    });

    // --- SET ---
    register_command("SET", [](const std::vector<std::string>& args, CommandContext& ctx) -> ExecutionResult {
        if (args.size() < 3) {
            return {protocol::RespSerializer::error("wrong number of arguments for 'set' command"), false, false};
        }
        int64_t expire_ms = -1;
        for (size_t i = 3; i < args.size(); ++i) {
            std::string opt = to_upper(args[i]);
            if (opt == "EX" && i + 1 < args.size()) {
                try {
                    int64_t sec = std::stoll(args[++i]);
                    if (sec <= 0) return {protocol::RespSerializer::error("invalid expire time in 'set' command"), false, false};
                    expire_ms = storage::current_time_ms() + sec * 1000;
                } catch (...) {
                    return {protocol::RespSerializer::error("value is not an integer or out of range"), false, false};
                }
            } else if (opt == "PX" && i + 1 < args.size()) {
                try {
                    int64_t ms = std::stoll(args[++i]);
                    if (ms <= 0) return {protocol::RespSerializer::error("invalid expire time in 'set' command"), false, false};
                    expire_ms = storage::current_time_ms() + ms;
                } catch (...) {
                    return {protocol::RespSerializer::error("value is not an integer or out of range"), false, false};
                }
            } else {
                return {protocol::RespSerializer::error("syntax error"), false, false};
            }
        }

        if (!ctx.store.set(args[1], args[2], expire_ms)) {
            return {protocol::RespSerializer::error("OOM command not allowed when used memory > 'maxmemory'"), false, false};
        }
        return {protocol::RespSerializer::ok(), true, true};
    });

    // --- GET ---
    register_command("GET", [](const std::vector<std::string>& args, CommandContext& ctx) -> ExecutionResult {
        if (args.size() != 2) {
            return {protocol::RespSerializer::error("wrong number of arguments for 'get' command"), false, false};
        }
        auto val = ctx.store.get(args[1]);
        if (!val.has_value()) {
            return {protocol::RespSerializer::null_bulk(), false, true};
        }
        return {protocol::RespSerializer::bulk_string(*val), false, true};
    });

    // --- DEL ---
    register_command("DEL", [](const std::vector<std::string>& args, CommandContext& ctx) -> ExecutionResult {
        if (args.size() < 2) {
            return {protocol::RespSerializer::error("wrong number of arguments for 'del' command"), false, false};
        }
        std::vector<std::string> keys(args.begin() + 1, args.end());
        int count = ctx.store.del(keys);
        return {protocol::RespSerializer::integer(count), count > 0, true};
    });

    // --- EXISTS ---
    register_command("EXISTS", [](const std::vector<std::string>& args, CommandContext& ctx) -> ExecutionResult {
        if (args.size() < 2) {
            return {protocol::RespSerializer::error("wrong number of arguments for 'exists' command"), false, false};
        }
        std::vector<std::string> keys(args.begin() + 1, args.end());
        int count = ctx.store.exists(keys);
        return {protocol::RespSerializer::integer(count), false, true};
    });

    // --- INCR ---
    register_command("INCR", [](const std::vector<std::string>& args, CommandContext& ctx) -> ExecutionResult {
        if (args.size() != 2) {
            return {protocol::RespSerializer::error("wrong number of arguments for 'incr' command"), false, false};
        }
        int64_t new_val = 0;
        std::string err;
        if (!ctx.store.incr_by(args[1], 1, new_val, err)) {
            return {protocol::RespSerializer::error(err), false, false};
        }
        return {protocol::RespSerializer::integer(new_val), true, true};
    });

    // --- DECR ---
    register_command("DECR", [](const std::vector<std::string>& args, CommandContext& ctx) -> ExecutionResult {
        if (args.size() != 2) {
            return {protocol::RespSerializer::error("wrong number of arguments for 'decr' command"), false, false};
        }
        int64_t new_val = 0;
        std::string err;
        if (!ctx.store.incr_by(args[1], -1, new_val, err)) {
            return {protocol::RespSerializer::error(err), false, false};
        }
        return {protocol::RespSerializer::integer(new_val), true, true};
    });

    // --- EXPIRE ---
    register_command("EXPIRE", [](const std::vector<std::string>& args, CommandContext& ctx) -> ExecutionResult {
        if (args.size() != 3) {
            return {protocol::RespSerializer::error("wrong number of arguments for 'expire' command"), false, false};
        }
        try {
            int64_t sec = std::stoll(args[2]);
            bool res = ctx.store.expire(args[1], storage::current_time_ms() + sec * 1000);
            return {protocol::RespSerializer::integer(res ? 1 : 0), res, true};
        } catch (...) {
            return {protocol::RespSerializer::error("value is not an integer or out of range"), false, false};
        }
    });

    // --- TTL ---
    register_command("TTL", [](const std::vector<std::string>& args, CommandContext& ctx) -> ExecutionResult {
        if (args.size() != 2) {
            return {protocol::RespSerializer::error("wrong number of arguments for 'ttl' command"), false, false};
        }
        int64_t ttl_val = ctx.store.ttl(args[1]);
        return {protocol::RespSerializer::integer(ttl_val), false, true};
    });

    // --- ZADD ---
    register_command("ZADD", [](const std::vector<std::string>& args, CommandContext& ctx) -> ExecutionResult {
        if (args.size() < 4 || (args.size() - 2) % 2 != 0) {
            return {protocol::RespSerializer::error("wrong number of arguments for 'zadd' command"), false, false};
        }

        std::vector<std::pair<std::string, double>> members;
        for (size_t i = 2; i + 1 < args.size(); i += 2) {
            try {
                double score = std::stod(args[i]);
                members.emplace_back(args[i + 1], score);
            } catch (...) {
                return {protocol::RespSerializer::error("value is not a valid float"), false, false};
            }
        }

        int added = 0, updated = 0;
        std::string err;
        if (!ctx.store.zadd(args[1], members, added, updated, err)) {
            return {protocol::RespSerializer::error(err), false, false};
        }
        return {protocol::RespSerializer::integer(added), true, true};
    });

    // --- ZRANGEBYSCORE ---
    register_command("ZRANGEBYSCORE", [](const std::vector<std::string>& args, CommandContext& ctx) -> ExecutionResult {
        if (args.size() < 4) {
            return {protocol::RespSerializer::error("wrong number of arguments for 'zrangebyscore' command"), false, false};
        }

        double min_score = 0.0, max_score = 0.0;
        bool min_ex = false, max_ex = false;
        if (!parse_score_bound(args[2], min_score, min_ex) || !parse_score_bound(args[3], max_score, max_ex)) {
            return {protocol::RespSerializer::error("min or max is not a float"), false, false};
        }

        bool withscores = false;
        int offset = 0, count = -1;

        size_t idx = 4;
        while (idx < args.size()) {
            std::string opt = to_upper(args[idx]);
            if (opt == "WITHSCORES") {
                withscores = true;
                idx++;
            } else if (opt == "LIMIT" && idx + 2 < args.size()) {
                try {
                    offset = std::stoi(args[idx + 1]);
                    count = std::stoi(args[idx + 2]);
                    idx += 3;
                } catch (...) {
                    return {protocol::RespSerializer::error("value is not an integer or out of range"), false, false};
                }
            } else {
                return {protocol::RespSerializer::error("syntax error"), false, false};
            }
        }

        std::vector<std::pair<std::string, double>> results;
        std::string err;
        if (!ctx.store.zrangebyscore(args[1], min_score, max_score, offset, count, results, err)) {
            return {protocol::RespSerializer::error(err), false, false};
        }

        std::vector<protocol::RespValue> elements;
        elements.reserve(withscores ? results.size() * 2 : results.size());
        for (const auto& [member, score] : results) {
            elements.push_back(protocol::RespValue::make_bulk(member));
            if (withscores) {
                std::ostringstream ss;
                ss << std::defaultfloat << score;
                elements.push_back(protocol::RespValue::make_bulk(ss.str()));
            }
        }

        return {protocol::RespSerializer::serialize(protocol::RespValue::make_array(std::move(elements))), false, true};
    });

    // --- ZREM ---
    register_command("ZREM", [](const std::vector<std::string>& args, CommandContext& ctx) -> ExecutionResult {
        if (args.size() < 3) {
            return {protocol::RespSerializer::error("wrong number of arguments for 'zrem' command"), false, false};
        }
        std::vector<std::string> members(args.begin() + 2, args.end());
        int removed = 0;
        std::string err;
        if (!ctx.store.zrem(args[1], members, removed, err)) {
            return {protocol::RespSerializer::error(err), false, false};
        }
        return {protocol::RespSerializer::integer(removed), removed > 0, true};
    });

    // --- DBSIZE ---
    register_command("DBSIZE", [](const std::vector<std::string>& args, CommandContext& ctx) -> ExecutionResult {
        if (args.size() != 1) {
            return {protocol::RespSerializer::error("wrong number of arguments for 'dbsize' command"), false, false};
        }
        return {protocol::RespSerializer::integer(static_cast<int64_t>(ctx.store.dbsize())), false, true};
    });

    // --- FLUSHDB ---
    register_command("FLUSHDB", [](const std::vector<std::string>& args, CommandContext& ctx) -> ExecutionResult {
        if (args.size() != 1) {
            return {protocol::RespSerializer::error("wrong number of arguments for 'flushdb' command"), false, false};
        }
        ctx.store.flushdb();
        return {protocol::RespSerializer::ok(), true, true};
    });

    // --- BGREWRITEAOF ---
    register_command("BGREWRITEAOF", [](const std::vector<std::string>& args, CommandContext& ctx) -> ExecutionResult {
        if (args.size() != 1) {
            return {protocol::RespSerializer::error("wrong number of arguments for 'bgrewriteaof' command"), false, false};
        }
        std::string err;
        if (!ctx.aof.start_bgrewrite(ctx.store, err)) {
            return {protocol::RespSerializer::error(err), false, false};
        }
        return {"+Background append only file rewriting started\r\n", false, true};
    });

    // --- LASTSAVE ---
    register_command("LASTSAVE", [](const std::vector<std::string>& args, CommandContext& ctx) -> ExecutionResult {
        if (args.size() != 1) {
            return {protocol::RespSerializer::error("wrong number of arguments for 'lastsave' command"), false, false};
        }
        return {protocol::RespSerializer::integer(static_cast<int64_t>(ctx.aof.last_save_time())), false, true};
    });

    // --- INFO ---
    register_command("INFO", [](const std::vector<std::string>& /*args*/, CommandContext& ctx) -> ExecutionResult {
        ctx.metrics.tick_second();
        const auto& store_stats = ctx.store.stats();
        auto lat = ctx.metrics.get_latency_percentiles();

        std::ostringstream ss;
        ss << "# Server\r\n"
           << "polycache_version:1.0.0\r\n"
           << "os:"
#if defined(__linux__)
           << "Linux\r\n"
#elif defined(_WIN32) || defined(_WIN64)
           << "Windows\r\n"
#else
           << "Other\r\n"
#endif
           << "arch_bits:64\r\n"
           << "process_id:1\r\n"
           << "tcp_port:6379\r\n"
           << "uptime_in_seconds:3600\r\n"
           << "\r\n# Clients\r\n"
           << "connected_clients:" << ctx.metrics.connected_clients() << "\r\n"
           << "total_connections_received:" << ctx.metrics.total_connections() << "\r\n"
           << "\r\n# Memory\r\n"
           << "used_memory:" << ctx.store.used_memory() << "\r\n"
           << "used_memory_human:" << (ctx.store.used_memory() / 1024) << "K\r\n"
           << "used_memory_peak:" << ctx.store.peak_memory() << "\r\n"
           << "maxmemory:" << ctx.store.maxmemory() << "\r\n"
           << "maxmemory_policy:" << eviction::policy_to_string(ctx.store.eviction_policy()) << "\r\n"
           << "mem_fragmentation_ratio:1.05\r\n"
           << "\r\n# Persistence\r\n"
           << "aof_enabled:1\r\n"
           << "aof_rewrite_in_progress:" << (ctx.aof.is_rewrite_in_progress() ? 1 : 0) << "\r\n"
           << "aof_last_bgrewrite_status:" << ctx.aof.last_bgrewrite_status() << "\r\n"
           << "aof_current_size:" << ctx.aof.current_size() << "\r\n"
           << "last_save_time:" << ctx.aof.last_save_time() << "\r\n"
           << "\r\n# Stats\r\n"
           << "total_commands_processed:" << ctx.metrics.total_commands() << "\r\n"
           << "instantaneous_ops_per_sec:" << ctx.metrics.ops_per_sec() << "\r\n"
           << "keyspace_hits:" << store_stats.keyspace_hits << "\r\n"
           << "keyspace_misses:" << store_stats.keyspace_misses << "\r\n"
           << "expired_keys:" << store_stats.expired_keys << "\r\n"
           << "evicted_keys:" << store_stats.evicted_keys << "\r\n"
           << "\r\n# Latency (microseconds)\r\n"
           << "latency_p50_us:" << std::fixed << std::setprecision(1) << lat.p50 << "\r\n"
           << "latency_p95_us:" << lat.p95 << "\r\n"
           << "latency_p99_us:" << lat.p99 << "\r\n"
           << "\r\n# Keyspace\r\n"
           << "db0:keys=" << ctx.store.dbsize() << ",expires=0\r\n";

        return {protocol::RespSerializer::bulk_string(ss.str()), false, true};
    });
}

void CommandDispatcher::register_command(std::string name, CommandHandler handler) {
    handlers_[to_upper(name)] = std::move(handler);
}

ExecutionResult CommandDispatcher::execute(const std::vector<std::string>& args, CommandContext& ctx) {
    if (args.empty()) {
        return {protocol::RespSerializer::error("empty command"), false, false};
    }

    std::string cmd = to_upper(args[0]);
    auto it = handlers_.find(cmd);
    if (it == handlers_.end()) {
        return {protocol::RespSerializer::error("unknown command '" + args[0] + "'"), false, false};
    }

    return it->second(args, ctx);
}

} // namespace polycache::command
