#include "src/storage/store.hpp"
#include <cassert>
#include <iostream>
#include <string>
#include <thread>

void test_store_lru_eviction() {
    using namespace polycache;

    // Configure a small memory limit (e.g. 5KB) so a few keys fit, and then eviction triggers
    eviction::EvictionConfig config;
    config.maxmemory = 4096; // 4KB
    config.policy = eviction::EvictionType::AllKeysLRU;
    config.sample_size = 5;

    storage::Store store(config);

    // Insert 50 keys with 100 bytes each
    std::string payload(100, 'x');
    for (int i = 0; i < 50; ++i) {
        store.set("key_" + std::to_string(i), payload);
    }

    // Current memory must be strictly bounded under or close to maxmemory
    assert(store.stats().evicted_keys > 0);
    assert(store.used_memory() <= config.maxmemory + 512);

    std::cout << "  [PASS] test_store_lru_eviction (evicted " << store.stats().evicted_keys << " keys)\n";
}

void test_store_ttl_expiration() {
    using namespace polycache;

    storage::Store store;

    // Set key with 100ms TTL
    store.set("temp_key", "temp_val", storage::current_time_ms() + 50);

    // Should be present immediately
    assert(store.get("temp_key").has_value());
    assert(*store.get("temp_key") == "temp_val");

    // Wait 70ms
    std::this_thread::sleep_for(std::chrono::milliseconds(70));

    // Lazy expiration should kick in on access
    assert(!store.get("temp_key").has_value());
    assert(store.stats().expired_keys == 1);

    std::cout << "  [PASS] test_store_ttl_expiration\n";
}
