#include "src/eviction/eviction.hpp"
#include <algorithm>

namespace polycache::eviction {

EvictionType parse_policy(std::string_view name) {
    if (name == "noeviction") return EvictionType::NoEviction;
    if (name == "allkeys-lru") return EvictionType::AllKeysLRU;
    if (name == "volatile-lru") return EvictionType::VolatileLRU;
    if (name == "allkeys-lfu") return EvictionType::AllKeysLFU;
    if (name == "volatile-lfu") return EvictionType::VolatileLFU;
    if (name == "allkeys-random") return EvictionType::AllKeysRandom;
    return EvictionType::AllKeysLRU;
}

std::string policy_to_string(EvictionType type) {
    switch (type) {
        case EvictionType::NoEviction: return "noeviction";
        case EvictionType::AllKeysLRU: return "allkeys-lru";
        case EvictionType::VolatileLRU: return "volatile-lru";
        case EvictionType::AllKeysLFU: return "allkeys-lfu";
        case EvictionType::VolatileLFU: return "volatile-lfu";
        case EvictionType::AllKeysRandom: return "allkeys-random";
    }
    return "unknown";
}

} // namespace polycache::eviction
