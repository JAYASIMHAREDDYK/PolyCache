#pragma once

#include "src/storage/dict.hpp"
#include "src/storage/entry.hpp"
#include "src/eviction/eviction.hpp"
#include <string>
#include <vector>
#include <optional>
#include <cstdint>
#include <atomic>

namespace polycache::storage {

struct StoreStats {
    uint64_t keyspace_hits{0};
    uint64_t keyspace_misses{0};
    uint64_t expired_keys{0};
    uint64_t evicted_keys{0};
    size_t peak_memory{0};
};

class Store {
public:
    Store(eviction::EvictionConfig config = {});
    ~Store() = default;

    Store(const Store&) = delete;
    Store& operator=(const Store&) = delete;

    // --- Core Operations ---
    bool set(const std::string& key, std::string val, int64_t expire_ms = -1);
    std::optional<std::string> get(const std::string& key);
    int del(const std::vector<std::string>& keys);
    int exists(const std::vector<std::string>& keys);

    // --- Counter Operations ---
    bool incr_by(const std::string& key, int64_t delta, int64_t& out_new_val, std::string& err);

    // --- Expiry Operations ---
    bool expire(const std::string& key, int64_t expire_ms);
    int64_t ttl(const std::string& key); // Returns -2 if missing, -1 if no TTL, else seconds

    // --- Sorted Sets ---
    bool zadd(const std::string& key, const std::vector<std::pair<std::string, double>>& elements,
              int& out_added, int& out_updated, std::string& err);
    bool zrem(const std::string& key, const std::vector<std::string>& members,
              int& out_removed, std::string& err);
    bool zrangebyscore(const std::string& key, double min_score, double max_score,
                       int offset, int count,
                       std::vector<std::pair<std::string, double>>& out_results,
                       std::string& err);

    // --- Server & Maintenance ---
    size_t dbsize();
    void flushdb();
    int active_expire_cycle(int timelimit_ms = 10);
    bool step_rehash(int n = 1);

    // --- Memory & Eviction ---
    size_t used_memory() const;
    size_t peak_memory() const { return stats_.peak_memory; }
    size_t maxmemory() const { return config_.maxmemory; }
    void set_maxmemory(size_t limit) { config_.maxmemory = limit; }
    void set_eviction_policy(eviction::EvictionType policy) { config_.policy = policy; }
    eviction::EvictionType eviction_policy() const { return config_.policy; }

    const StoreStats& stats() const { return stats_; }
    Dict<ValueEntry>& raw_dict() { return dict_; }
    const Dict<ValueEntry>& raw_dict() const { return dict_; }

private:
    bool evict_if_needed();
    ValueEntry* lookup_key_write(const std::string& key);
    ValueEntry* lookup_key_read(const std::string& key);
    void update_memory(int64_t delta);

    Dict<ValueEntry> dict_;
    eviction::EvictionConfig config_;
    StoreStats stats_;
    size_t current_memory_{sizeof(Store)};
};

} // namespace polycache::storage
