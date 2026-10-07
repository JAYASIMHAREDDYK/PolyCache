#include "src/storage/store.hpp"
#include <charconv>
#include <algorithm>
#include <iostream>

namespace polycache::storage {

Store::Store(eviction::EvictionConfig config)
    : config_(config) {
    current_memory_ = sizeof(Store) + dict_.memory_overhead();
    stats_.peak_memory = current_memory_;
}

void Store::update_memory(int64_t delta) {
    if (delta < 0 && static_cast<size_t>(-delta) > current_memory_) {
        current_memory_ = 0;
    } else {
        current_memory_ += delta;
    }
    if (current_memory_ > stats_.peak_memory) {
        stats_.peak_memory = current_memory_;
    }
}

size_t Store::used_memory() const {
    return current_memory_;
}

ValueEntry* Store::lookup_key_read(const std::string& key) {
    ValueEntry* entry = dict_.find(key);
    if (!entry) {
        stats_.keyspace_misses++;
        return nullptr;
    }

    if (entry->is_expired()) {
        dict_.erase(key);
        stats_.expired_keys++;
        stats_.keyspace_misses++;
        return nullptr;
    }

    entry->touch();
    stats_.keyspace_hits++;
    return entry;
}

ValueEntry* Store::lookup_key_write(const std::string& key) {
    ValueEntry* entry = dict_.find(key);
    if (!entry) return nullptr;

    if (entry->is_expired()) {
        dict_.erase(key);
        stats_.expired_keys++;
        return nullptr;
    }

    entry->touch();
    return entry;
}

bool Store::evict_if_needed() {
    if (config_.maxmemory == 0 || current_memory_ <= config_.maxmemory) {
        return true;
    }
    if (config_.policy == eviction::EvictionType::NoEviction) {
        return false;
    }

    uint32_t now = current_time_sec();

    while (current_memory_ > config_.maxmemory && dict_.size() > 0) {
        std::string best_key;
        int64_t best_score = -1;

        for (int i = 0; i < config_.sample_size; ++i) {
            auto* entry = dict_.get_random_entry();
            if (!entry) break;

            bool is_volatile = (entry->val.expire_at_ms >= 0);
            if ((config_.policy == eviction::EvictionType::VolatileLRU ||
                 config_.policy == eviction::EvictionType::VolatileLFU) && !is_volatile) {
                continue;
            }

            int64_t score = 0;
            if (config_.policy == eviction::EvictionType::AllKeysLRU ||
                config_.policy == eviction::EvictionType::VolatileLRU) {
                // Higher idle time is preferred for eviction
                score = static_cast<int64_t>(now >= entry->val.lru_sec ? now - entry->val.lru_sec : 0);
            } else if (config_.policy == eviction::EvictionType::AllKeysLFU ||
                       config_.policy == eviction::EvictionType::VolatileLFU) {
                // Lower frequency counter preferred for eviction
                score = 255 - static_cast<int64_t>(entry->val.lfu_counter);
            } else { // Random
                score = 100;
            }

            if (score > best_score || best_key.empty()) {
                best_score = score;
                best_key = entry->key;
            }
        }

        if (best_key.empty()) {
            // No eligible key found
            return false;
        }

        auto* victim = dict_.find(best_key);
        if (victim) {
            size_t freed = best_key.size() + sizeof(DictEntry<ValueEntry>) + victim->memory_bytes();
            update_memory(-static_cast<int64_t>(freed));
            dict_.erase(best_key);
            stats_.evicted_keys++;
        }
    }

    return current_memory_ <= config_.maxmemory;
}

bool Store::set(const std::string& key, std::string val, int64_t expire_ms) {
    if (!evict_if_needed()) {
        return false;
    }

    auto* existing = lookup_key_write(key);
    size_t old_bytes = 0;
    if (existing) {
        old_bytes = key.size() + existing->memory_bytes();
    }

    ValueEntry new_val = ValueEntry::make_string(std::move(val), expire_ms);
    size_t new_bytes = key.size() + sizeof(DictEntry<ValueEntry>) + new_val.memory_bytes();

    dict_.insert_or_assign(key, std::move(new_val));
    update_memory(static_cast<int64_t>(new_bytes) - static_cast<int64_t>(old_bytes));
    return true;
}

std::optional<std::string> Store::get(const std::string& key) {
    auto* entry = lookup_key_read(key);
    if (!entry || entry->type != EntryType::String) {
        return std::nullopt;
    }
    return entry->string_val;
}

int Store::del(const std::vector<std::string>& keys) {
    int count = 0;
    for (const auto& key : keys) {
        auto* entry = dict_.find(key);
        if (entry) {
            size_t freed = key.size() + sizeof(DictEntry<ValueEntry>) + entry->memory_bytes();
            update_memory(-static_cast<int64_t>(freed));
            dict_.erase(key);
            count++;
        }
    }
    return count;
}

int Store::exists(const std::vector<std::string>& keys) {
    int count = 0;
    for (const auto& key : keys) {
        if (lookup_key_read(key)) {
            count++;
        }
    }
    return count;
}

bool Store::incr_by(const std::string& key, int64_t delta, int64_t& out_new_val, std::string& err) {
    auto* entry = lookup_key_write(key);
    if (!entry) {
        // Key doesn't exist, create initialized to delta
        if (!evict_if_needed()) {
            err = "OOM command not allowed when used memory > 'maxmemory'";
            return false;
        }
        out_new_val = delta;
        set(key, std::to_string(delta));
        return true;
    }

    if (entry->type != EntryType::String) {
        err = "WRONGTYPE Operation against a key holding the wrong kind of value";
        return false;
    }

    int64_t current_val = 0;
    auto [ptr, ec] = std::from_chars(entry->string_val.data(),
                                     entry->string_val.data() + entry->string_val.size(),
                                     current_val);
    if (ec != std::errc() || ptr != entry->string_val.data() + entry->string_val.size()) {
        err = "ERR value is not an integer or out of range";
        return false;
    }

    out_new_val = current_val + delta;
    size_t old_bytes = entry->memory_bytes();
    entry->string_val = std::to_string(out_new_val);
    size_t new_bytes = entry->memory_bytes();
    update_memory(static_cast<int64_t>(new_bytes) - static_cast<int64_t>(old_bytes));
    return true;
}

bool Store::expire(const std::string& key, int64_t expire_ms) {
    auto* entry = lookup_key_write(key);
    if (!entry) return false;
    entry->expire_at_ms = expire_ms;
    return true;
}

int64_t Store::ttl(const std::string& key) {
    auto* entry = lookup_key_read(key);
    if (!entry) return -2; // Not found
    if (entry->expire_at_ms < 0) return -1; // No TTL

    int64_t now_ms = current_time_ms();
    int64_t remaining_ms = entry->expire_at_ms - now_ms;
    if (remaining_ms <= 0) return -2;
    return remaining_ms / 1000;
}

bool Store::zadd(const std::string& key, const std::vector<std::pair<std::string, double>>& elements,
                 int& out_added, int& out_updated, std::string& err) {
    if (!evict_if_needed()) {
        err = "OOM command not allowed when used memory > 'maxmemory'";
        return false;
    }

    auto* entry = lookup_key_write(key);
    if (!entry) {
        ValueEntry new_entry = ValueEntry::make_zset();
        dict_.insert_or_assign(key, std::move(new_entry));
        entry = dict_.find(key);
        update_memory(key.size() + sizeof(DictEntry<ValueEntry>) + sizeof(ValueEntry));
    }

    if (entry->type != EntryType::ZSet || !entry->zset_val) {
        err = "WRONGTYPE Operation against a key holding the wrong kind of value";
        return false;
    }

    out_added = 0;
    out_updated = 0;
    size_t old_bytes = entry->zset_val->memory_overhead();

    for (const auto& [member, score] : elements) {
        auto existing_score = entry->zset_val->get_score(member);
        if (!existing_score.has_value()) {
            entry->zset_val->insert(member, score);
            out_added++;
        } else {
            if (*existing_score != score) {
                entry->zset_val->insert(member, score);
                out_updated++;
            }
        }
    }

    size_t new_bytes = entry->zset_val->memory_overhead();
    update_memory(static_cast<int64_t>(new_bytes) - static_cast<int64_t>(old_bytes));
    return true;
}

bool Store::zrem(const std::string& key, const std::vector<std::string>& members,
                 int& out_removed, std::string& err) {
    auto* entry = lookup_key_write(key);
    if (!entry) {
        out_removed = 0;
        return true;
    }

    if (entry->type != EntryType::ZSet || !entry->zset_val) {
        err = "WRONGTYPE Operation against a key holding the wrong kind of value";
        return false;
    }

    out_removed = 0;
    size_t old_bytes = entry->zset_val->memory_overhead();

    for (const auto& member : members) {
        if (entry->zset_val->remove(member)) {
            out_removed++;
        }
    }

    size_t new_bytes = entry->zset_val->memory_overhead();
    update_memory(static_cast<int64_t>(new_bytes) - static_cast<int64_t>(old_bytes));

    if (entry->zset_val->empty()) {
        del({key});
    }

    return true;
}

bool Store::zrangebyscore(const std::string& key, double min_score, double max_score,
                          int offset, int count,
                          std::vector<std::pair<std::string, double>>& out_results,
                          std::string& err) {
    auto* entry = lookup_key_read(key);
    if (!entry) {
        return true; // Empty result
    }

    if (entry->type != EntryType::ZSet || !entry->zset_val) {
        err = "WRONGTYPE Operation against a key holding the wrong kind of value";
        return false;
    }

    auto items = entry->zset_val->range_by_score(min_score, max_score, offset, count);
    out_results.reserve(items.size());
    for (const auto& item : items) {
        out_results.emplace_back(item.member, item.score);
    }
    return true;
}

size_t Store::dbsize() {
    return dict_.size();
}

void Store::flushdb() {
    dict_.clear();
    current_memory_ = sizeof(Store) + dict_.memory_overhead();
}

int Store::active_expire_cycle(int timelimit_ms) {
    if (dict_.empty()) return 0;

    auto start = std::chrono::steady_clock::now();
    int expired_count = 0;

    while (true) {
        int sampled = 0;
        int expired_in_sample = 0;
        int64_t now_ms = current_time_ms();

        for (int i = 0; i < 20; ++i) {
            auto* entry = dict_.get_random_entry();
            if (!entry) break;
            sampled++;

            if (entry->val.expire_at_ms >= 0) {
                if (now_ms >= entry->val.expire_at_ms) {
                    size_t freed = entry->key.size() + sizeof(DictEntry<ValueEntry>) + entry->val.memory_bytes();
                    update_memory(-static_cast<int64_t>(freed));
                    dict_.erase(entry->key);
                    expired_in_sample++;
                    expired_count++;
                    stats_.expired_keys++;
                }
            }
        }

        if (sampled == 0) break;
        // If expired keys in sample were <= 25%, break
        if (expired_in_sample * 4 <= sampled) break;

        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count() >= timelimit_ms) {
            break;
        }
    }

    return expired_count;
}

bool Store::step_rehash(int n) {
    return dict_.rehash_step(n);
}

} // namespace polycache::storage
