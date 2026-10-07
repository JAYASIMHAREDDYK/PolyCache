#include "src/storage/dict.hpp"
#include <cassert>
#include <iostream>
#include <string>
#include <vector>

void test_dict_basic() {
    polycache::storage::Dict<std::string> d;
    assert(d.empty());
    assert(d.size() == 0);

    assert(d.insert_or_assign("key1", "val1") == true);
    assert(d.size() == 1);
    assert(d.find("key1") != nullptr);
    assert(*d.find("key1") == "val1");

    // Update existing
    assert(d.insert_or_assign("key1", "val1_updated") == false);
    assert(d.size() == 1);
    assert(*d.find("key1") == "val1_updated");

    // Missing
    assert(d.find("nonexistent") == nullptr);

    // Erase
    assert(d.erase("key1") == true);
    assert(d.size() == 0);
    assert(d.find("key1") == nullptr);
    assert(d.erase("key1") == false);

    std::cout << "  [PASS] test_dict_basic\n";
}

void test_dict_incremental_rehashing() {
    polycache::storage::Dict<int> d;

    // Insert 1000 items, which will trigger several expansions and rehash cycles
    for (int i = 0; i < 1000; ++i) {
        d.insert_or_assign("k_" + std::to_string(i), i);
        // Step rehash along the way
        if (d.is_rehashing()) {
            d.rehash_step(2);
        }
    }

    assert(d.size() == 1000);

    // Ensure all 1000 keys are retrievable during/after rehashing
    for (int i = 0; i < 1000; ++i) {
        int* v = d.find("k_" + std::to_string(i));
        assert(v != nullptr);
        assert(*v == i);
    }

    // Finish any remaining rehash steps
    while (d.is_rehashing()) {
        d.rehash_step(10);
    }
    assert(!d.is_rehashing());

    // Test random sampling
    for (int i = 0; i < 50; ++i) {
        auto* entry = d.get_random_entry();
        assert(entry != nullptr);
        assert(!entry->key.empty());
    }

    std::cout << "  [PASS] test_dict_incremental_rehashing\n";
}
