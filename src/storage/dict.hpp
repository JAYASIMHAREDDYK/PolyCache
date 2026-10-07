#pragma once

#include <string>
#include <string_view>
#include <cstdint>
#include <cstddef>
#include <functional>
#include <vector>
#include <random>
#include <chrono>

namespace polycache::storage {

// MurmurHash2 or FNV-1a for 64-bit string hash
inline uint64_t hash_string(std::string_view key) {
    uint64_t hash = 14695981039346656037ULL;
    for (char c : key) {
        hash ^= static_cast<uint8_t>(c);
        hash *= 1099511628211ULL;
    }
    return hash;
}

template <typename ValueType>
struct DictEntry {
    std::string key;
    ValueType val;
    DictEntry* next{nullptr};

    DictEntry(std::string k, ValueType v, DictEntry* n = nullptr)
        : key(std::move(k)), val(std::move(v)), next(n) {}
};

template <typename ValueType>
class Dict {
public:
    static constexpr size_t INITIAL_SIZE = 16;

    Dict() {
        table_[0] = {};
        table_[1] = {};
        rehashidx_ = -1;
    }

    ~Dict() {
        clear();
    }

    Dict(const Dict&) = delete;
    Dict& operator=(const Dict&) = delete;

    Dict(Dict&& other) noexcept {
        table_[0] = other.table_[0];
        table_[1] = other.table_[1];
        rehashidx_ = other.rehashidx_;
        other.table_[0] = {};
        other.table_[1] = {};
        other.rehashidx_ = -1;
    }

    Dict& operator=(Dict&& other) noexcept {
        if (this != &other) {
            clear();
            table_[0] = other.table_[0];
            table_[1] = other.table_[1];
            rehashidx_ = other.rehashidx_;
            other.table_[0] = {};
            other.table_[1] = {};
            other.rehashidx_ = -1;
        }
        return *this;
    }

    size_t size() const {
        return table_[0].used + table_[1].used;
    }

    bool empty() const {
        return size() == 0;
    }

    bool is_rehashing() const {
        return rehashidx_ != -1;
    }

    int rehash_idx() const {
        return rehashidx_;
    }

    // Step-wise incremental rehash. Returns true if more rehashing is needed.
    bool rehash_step(int n = 1) {
        if (!is_rehashing()) return false;

        while (n-- && table_[0].used > 0) {
            while (table_[0].table[rehashidx_] == nullptr) {
                rehashidx_++;
                if (rehashidx_ >= static_cast<int>(table_[0].size)) {
                    finish_rehash();
                    return false;
                }
            }

            DictEntry<ValueType>* de = table_[0].table[rehashidx_];
            while (de != nullptr) {
                DictEntry<ValueType>* nextde = de->next;
                uint64_t h = hash_string(de->key) & table_[1].sizemask;
                de->next = table_[1].table[h];
                table_[1].table[h] = de;
                table_[0].used--;
                table_[1].used++;
                de = nextde;
            }
            table_[0].table[rehashidx_] = nullptr;
            rehashidx_++;
        }

        // Check if finished
        if (table_[0].used == 0) {
            finish_rehash();
            return false;
        }

        return true;
    }

    // Perform rehashing for a maximum duration (milliseconds)
    int rehash_milliseconds(int ms) {
        if (!is_rehashing()) return 0;
        auto start = std::chrono::steady_clock::now();
        int steps = 0;
        while (is_rehashing()) {
            rehash_step(10);
            steps += 10;
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count() >= ms) {
                break;
            }
        }
        return steps;
    }

    // Insert or update
    bool insert_or_assign(const std::string& key, ValueType val) {
        if (is_rehashing()) {
            rehash_step(1);
        }

        // Check if resize needed
        expand_if_needed();

        uint64_t h = hash_string(key);
        // Search in both tables to update if already exists
        for (int table_idx = 0; table_idx <= 1; ++table_idx) {
            if (table_[table_idx].size == 0) continue;
            size_t idx = h & table_[table_idx].sizemask;
            DictEntry<ValueType>* entry = table_[table_idx].table[idx];
            while (entry) {
                if (entry->key == key) {
                    entry->val = std::move(val);
                    return false; // Existing key updated
                }
                entry = entry->next;
            }
            if (!is_rehashing()) break;
        }

        // Insert new entry into table_[1] if rehashing, else table_[0]
        int target = is_rehashing() ? 1 : 0;
        size_t idx = h & table_[target].sizemask;
        auto* new_entry = new DictEntry<ValueType>(key, std::move(val), table_[target].table[idx]);
        table_[target].table[idx] = new_entry;
        table_[target].used++;
        return true; // Newly inserted
    }

    ValueType* find(std::string_view key) {
        if (table_[0].size == 0) return nullptr;
        if (is_rehashing()) {
            rehash_step(1);
        }

        uint64_t h = hash_string(key);
        for (int table_idx = 0; table_idx <= 1; ++table_idx) {
            if (table_[table_idx].size == 0) break;
            size_t idx = h & table_[table_idx].sizemask;
            DictEntry<ValueType>* entry = table_[table_idx].table[idx];
            while (entry) {
                if (entry->key == key) {
                    return &entry->val;
                }
                entry = entry->next;
            }
            if (!is_rehashing()) break;
        }
        return nullptr;
    }

    const ValueType* find(std::string_view key) const {
        if (table_[0].size == 0) return nullptr;
        uint64_t h = hash_string(key);
        for (int table_idx = 0; table_idx <= 1; ++table_idx) {
            if (table_[table_idx].size == 0) break;
            size_t idx = h & table_[table_idx].sizemask;
            DictEntry<ValueType>* entry = table_[table_idx].table[idx];
            while (entry) {
                if (entry->key == key) {
                    return &entry->val;
                }
                entry = entry->next;
            }
            if (!is_rehashing()) break;
        }
        return nullptr;
    }

    bool erase(std::string_view key) {
        if (table_[0].size == 0) return false;
        if (is_rehashing()) {
            rehash_step(1);
        }

        uint64_t h = hash_string(key);
        for (int table_idx = 0; table_idx <= 1; ++table_idx) {
            if (table_[table_idx].size == 0) break;
            size_t idx = h & table_[table_idx].sizemask;
            DictEntry<ValueType>* entry = table_[table_idx].table[idx];
            DictEntry<ValueType>* prev = nullptr;

            while (entry) {
                if (entry->key == key) {
                    if (prev) {
                        prev->next = entry->next;
                    } else {
                        table_[table_idx].table[idx] = entry->next;
                    }
                    delete entry;
                    table_[table_idx].used--;
                    return true;
                }
                prev = entry;
                entry = entry->next;
            }
            if (!is_rehashing()) break;
        }
        return false;
    }

    // Returns a random entry (for approximate LRU/LFU eviction sampling)
    DictEntry<ValueType>* get_random_entry() {
        if (empty()) return nullptr;
        if (is_rehashing()) {
            rehash_step(1);
        }

        static thread_local std::mt19937_64 rng(1337);
        size_t total_slots = table_[0].size + table_[1].size;
        if (total_slots == 0) return nullptr;

        // Try random slots until a non-empty bucket is found
        for (int attempts = 0; attempts < 100; ++attempts) {
            size_t slot = rng() % total_slots;
            DictEntry<ValueType>* entry = nullptr;
            if (slot < table_[0].size) {
                entry = table_[0].table[slot];
            } else {
                entry = table_[1].table[slot - table_[0].size];
            }

            if (entry) {
                // Pick a node along the chain
                std::vector<DictEntry<ValueType>*> chain;
                while (entry) {
                    chain.push_back(entry);
                    entry = entry->next;
                }
                return chain[rng() % chain.size()];
            }
        }

        // Fallback linear scan if sparse
        for (int t = 0; t <= 1; ++t) {
            for (size_t i = 0; i < table_[t].size; ++i) {
                if (table_[t].table[i]) return table_[t].table[i];
            }
        }
        return nullptr;
    }

    template <typename Callback>
    void for_each(Callback&& cb) const {
        for (int t = 0; t <= 1; ++t) {
            if (table_[t].size == 0) continue;
            for (size_t i = 0; i < table_[t].size; ++i) {
                DictEntry<ValueType>* entry = table_[t].table[i];
                while (entry) {
                    cb(entry->key, entry->val);
                    entry = entry->next;
                }
            }
        }
    }

    void clear() {
        for (int t = 0; t <= 1; ++t) {
            if (table_[t].table) {
                for (size_t i = 0; i < table_[t].size; ++i) {
                    DictEntry<ValueType>* entry = table_[t].table[i];
                    while (entry) {
                        DictEntry<ValueType>* next = entry->next;
                        delete entry;
                        entry = next;
                    }
                }
                delete[] table_[t].table;
                table_[t].table = nullptr;
            }
            table_[t].size = 0;
            table_[t].sizemask = 0;
            table_[t].used = 0;
        }
        rehashidx_ = -1;
    }

    size_t memory_overhead() const {
        size_t bytes = sizeof(*this);
        bytes += table_[0].size * sizeof(DictEntry<ValueType>*);
        bytes += table_[1].size * sizeof(DictEntry<ValueType>*);
        bytes += (table_[0].used + table_[1].used) * sizeof(DictEntry<ValueType>);
        return bytes;
    }

private:
    struct Table {
        DictEntry<ValueType>** table{nullptr};
        size_t size{0};
        size_t sizemask{0};
        size_t used{0};
    };

    Table table_[2];
    int rehashidx_{-1};

    void expand_if_needed() {
        if (is_rehashing()) return;

        if (table_[0].size == 0) {
            init_table(table_[0], INITIAL_SIZE);
            return;
        }

        // Rehash threshold: load factor >= 1.0
        if (table_[0].used >= table_[0].size) {
            size_t new_size = table_[0].size * 2;
            init_table(table_[1], new_size);
            rehashidx_ = 0;
        }
    }

    void init_table(Table& t, size_t size) {
        t.size = size;
        t.sizemask = size - 1;
        t.used = 0;
        t.table = new DictEntry<ValueType>*[size]();
    }

    void finish_rehash() {
        delete[] table_[0].table;
        table_[0] = table_[1];
        table_[1] = {};
        rehashidx_ = -1;
    }
};

} // namespace polycache::storage
