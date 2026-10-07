#pragma once

#include <string>
#include <string_view>
#include <cstdint>
#include <cstddef>

namespace polycache::eviction {

enum class EvictionType {
    NoEviction,
    AllKeysLRU,
    VolatileLRU,
    AllKeysLFU,
    VolatileLFU,
    AllKeysRandom
};

EvictionType parse_policy(std::string_view name);
std::string policy_to_string(EvictionType type);

struct EvictionConfig {
    size_t maxmemory{0}; // 0 = unlimited
    EvictionType policy{EvictionType::AllKeysLRU};
    int sample_size{5};  // Redis default is 5
};

} // namespace polycache::eviction
