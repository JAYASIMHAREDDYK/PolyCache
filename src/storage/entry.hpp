#pragma once

#include "src/storage/skiplist.hpp"
#include <string>
#include <memory>
#include <chrono>
#include <cstdint>

namespace polycache::storage {

enum class EntryType {
    String,
    ZSet
};

inline uint32_t current_time_sec() {
    return static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
}

inline int64_t current_time_ms() {
    return static_cast<int64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
}

struct ValueEntry {
    EntryType type{EntryType::String};
    std::string string_val;
    std::unique_ptr<SkipList> zset_val;

    int64_t expire_at_ms{-1}; // -1 if no expiry
    uint32_t lru_sec{0};      // Access timestamp for approximate LRU
    uint8_t lfu_counter{5};   // Redis-style initial LFU counter
    uint16_t lfu_ldt_min{0};  // Last decay time in minutes

    ValueEntry() : lru_sec(current_time_sec()), lfu_ldt_min(static_cast<uint16_t>((lru_sec / 60) & 65535)) {}

    static ValueEntry make_string(std::string s, int64_t expire_ms = -1) {
        ValueEntry v;
        v.type = EntryType::String;
        v.string_val = std::move(s);
        v.expire_at_ms = expire_ms;
        v.lru_sec = current_time_sec();
        v.lfu_counter = 5;
        v.lfu_ldt_min = static_cast<uint16_t>((v.lru_sec / 60) & 65535);
        return v;
    }

    static ValueEntry make_zset(int64_t expire_ms = -1) {
        ValueEntry v;
        v.type = EntryType::ZSet;
        v.zset_val = std::make_unique<SkipList>();
        v.expire_at_ms = expire_ms;
        v.lru_sec = current_time_sec();
        v.lfu_counter = 5;
        v.lfu_ldt_min = static_cast<uint16_t>((v.lru_sec / 60) & 65535);
        return v;
    }

    bool is_expired(int64_t now_ms = -1) const {
        if (expire_at_ms < 0) return false;
        if (now_ms < 0) now_ms = current_time_ms();
        return now_ms >= expire_at_ms;
    }

    void touch() {
        uint32_t now = current_time_sec();
        lru_sec = now;

        // LFU logarithmic frequency decay & increment
        uint16_t now_min = static_cast<uint16_t>((now / 60) & 65535);
        uint16_t num_periods = (now_min >= lfu_ldt_min) ? (now_min - lfu_ldt_min) : (65535 - lfu_ldt_min + now_min);
        if (num_periods > 0) {
            if (lfu_counter > num_periods) {
                lfu_counter -= static_cast<uint8_t>(num_periods);
            } else {
                lfu_counter = 0;
            }
            lfu_ldt_min = now_min;
        }

        // Probabilistic logarithmic counter bump
        if (lfu_counter < 255) {
            lfu_counter++;
        }
    }

    size_t memory_bytes() const {
        size_t bytes = sizeof(*this);
        if (type == EntryType::String) {
            bytes += string_val.capacity();
        } else if (type == EntryType::ZSet && zset_val) {
            bytes += zset_val->memory_overhead();
        }
        return bytes;
    }
};

} // namespace polycache::storage
