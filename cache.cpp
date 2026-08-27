#include <iostream>
#include <unordered_map>
#include <list>
#include <string>
#include <vector>
#include <random>
#include <memory>
#include <iomanip>
#include <optional>
#include <cassert>

class EvictionPolicy {
public:
    virtual ~EvictionPolicy() = default;
    virtual void onAccess(int key) = 0;
    virtual void onInsert(int key) = 0;
    virtual int evict() = 0;
    virtual void onRemove(int key) = 0;
};

class OrderedListPolicy : public EvictionPolicy {
protected:
    std::list<int> order;
    std::unordered_map<int, std::list<int>::iterator> pos;

public:
    void onInsert(int key) override {
        order.push_front(key);
        pos[key] = order.begin();
    }

    int evict() override {
        int victim = order.back();
        pos.erase(victim);
        order.pop_back();
        return victim;
    }

    void onRemove(int key) override {
        auto it = pos.find(key);
        if (it != pos.end()) {
            order.erase(it->second);
            pos.erase(it);
        }
    }
};

class FIFOPolicy : public OrderedListPolicy {
public:
    void onAccess(int) override {}
};

class LRUPolicy : public OrderedListPolicy {
public:
    void onAccess(int key) override {
        auto it = pos.find(key);
        if (it == pos.end()) return;
        order.erase(it->second);
        order.push_front(key);
        it->second = order.begin();
    }
};

class LFUPolicy : public EvictionPolicy {
    std::unordered_map<int, int> freq;
    std::unordered_map<int, std::list<int>> buckets;
    std::unordered_map<int, std::list<int>::iterator> keyPos;
    int minFreq = 0;

public:
    void onAccess(int key) override {
        auto it = freq.find(key);
        if (it == freq.end()) return;

        int oldF = it->second;
        int newF = oldF + 1;
        it->second = newF;

        buckets.at(oldF).erase(keyPos.at(key));
        if (buckets.at(oldF).empty()) {
            buckets.erase(oldF);
            if (minFreq == oldF) minFreq = newF;
        }

        buckets[newF].push_back(key);
        keyPos[key] = std::prev(buckets[newF].end());
    }

    void onInsert(int key) override {
        freq[key] = 1;
        minFreq = 1;
        buckets[1].push_back(key);
        keyPos[key] = std::prev(buckets[1].end());
    }

    int evict() override {
        auto& b = buckets.at(minFreq);
        int victim = b.front();
        b.pop_front();
        if (b.empty()) buckets.erase(minFreq);
        freq.erase(victim);
        keyPos.erase(victim);
        return victim;
    }

    void onRemove(int key) override {
        auto it = freq.find(key);
        if (it == freq.end()) return;
        int f = it->second;
        buckets.at(f).erase(keyPos.at(key));
        if (buckets.at(f).empty()) buckets.erase(f);
        freq.erase(key);
        keyPos.erase(key);
    }
};

class Cache {
    int capacity;
    std::unordered_map<int, int> store;
    std::unique_ptr<EvictionPolicy> policy;

public:
    Cache(int cap, std::unique_ptr<EvictionPolicy> p)
        : capacity(cap), policy(std::move(p)) {
        assert(capacity > 0);
    }

    std::optional<int> get(int key) {
        auto it = store.find(key);
        if (it == store.end()) return std::nullopt;
        policy->onAccess(key);
        return it->second;
    }

    void put(int key, int value) {
        if (store.count(key)) {
            store[key] = value;
            policy->onAccess(key);
            return;
        }
        if (static_cast<int>(store.size()) >= capacity) {
            int victim = policy->evict();
            store.erase(victim);
        }
        store[key] = value;
        policy->onInsert(key);
    }

    int size() const { return static_cast<int>(store.size()); }
};

std::unique_ptr<EvictionPolicy> createPolicy(const std::string& type) {
    if (type == "LRU") return std::make_unique<LRUPolicy>();
    if (type == "LFU") return std::make_unique<LFUPolicy>();
    if (type == "FIFO") return std::make_unique<FIFOPolicy>();
    throw std::runtime_error("unknown policy: " + type);
}

struct Result {
    std::string policy, pattern;
    int hits, total;
    double hitRate;
};

Result runBenchmark(const std::string& pol, const std::string& pat,
                    int cap, const std::vector<int>& keys) {
    Cache cache(cap, createPolicy(pol));
    int hits = 0;
    for (int k : keys) {
        if (cache.get(k).has_value()) hits++;
        else cache.put(k, k * 10);
    }
    double rate = keys.empty() ? 0.0 : static_cast<double>(hits) / keys.size() * 100.0;
    return {pol, pat, hits, static_cast<int>(keys.size()), rate};
}

int main() {
    std::cout << "=== LRU demo (cap=3) ===" << std::endl;
    Cache lru(3, createPolicy("LRU"));
    lru.put(1, 100); lru.put(2, 200); lru.put(3, 300);
    std::cout << "get(1) = " << lru.get(1).value() << std::endl;
    lru.put(4, 400);
    std::cout << "get(2) = " << (lru.get(2).has_value() ? std::to_string(lru.get(2).value()) : "miss") << std::endl;
    std::cout << "get(3) = " << lru.get(3).value() << std::endl;
    std::cout << "get(4) = " << lru.get(4).value() << std::endl;

    std::cout << "\n=== FIFO demo (cap=3) ===" << std::endl;
    Cache fifo(3, createPolicy("FIFO"));
    fifo.put(1, 100); fifo.put(2, 200); fifo.put(3, 300);
    std::cout << "get(1) = " << fifo.get(1).value() << std::endl;
    fifo.put(4, 400);
    std::cout << "get(1) = " << (fifo.get(1).has_value() ? std::to_string(fifo.get(1).value()) : "miss") << std::endl;
    std::cout << "get(2) = " << fifo.get(2).value() << std::endl;

    std::cout << "\n=== LFU demo (cap=3) ===" << std::endl;
    Cache lfu(3, createPolicy("LFU"));
    lfu.put(1, 100); lfu.put(2, 200); lfu.put(3, 300);
    lfu.get(1); lfu.get(1); lfu.get(1);
    lfu.get(2);
    lfu.put(4, 400);
    std::cout << "get(3) = " << (lfu.get(3).has_value() ? std::to_string(lfu.get(3).value()) : "miss") << std::endl;
    std::cout << "get(1) = " << lfu.get(1).value() << std::endl;

    std::cout << "\n=== benchmarks (cap=50, 10k accesses, keys 0-199) ===" << std::endl;
    const int CAP = 50, N = 10000;
    std::mt19937 rng(42);

    std::vector<int> seq(N), rand_acc(N), hot(N);
    for (int i = 0; i < N; i++) seq[i] = i % 200;

    std::uniform_int_distribution<int> d200(0, 199);
    for (int i = 0; i < N; i++) rand_acc[i] = d200(rng);

    std::uniform_real_distribution<double> coin(0, 1);
    std::uniform_int_distribution<int> dHot(0, 9), dCold(10, 199);
    for (int i = 0; i < N; i++)
        hot[i] = (coin(rng) < 0.8) ? dHot(rng) : dCold(rng);

    std::vector<std::string> pols = {"LRU", "LFU", "FIFO"};
    std::vector<std::pair<std::string, std::vector<int>*>> pats = {
        {"Sequential", &seq}, {"Random", &rand_acc}, {"HotKey80/20", &hot}
    };

    std::cout << std::left << std::setw(8) << "Policy" << std::setw(14) << "Pattern"
              << std::setw(7) << "Hits" << std::setw(7) << "Total" << "Rate" << std::endl;
    std::cout << std::string(42, '-') << std::endl;

    int lruHot = -1, lfuHot = -1, fifoHot = -1;

    for (auto& [pn, pd] : pats) {
        for (auto& p : pols) {
            auto r = runBenchmark(p, pn, CAP, *pd);
            std::cout << std::left << std::setw(8) << r.policy << std::setw(14) << r.pattern
                      << std::setw(7) << r.hits << std::setw(7) << r.total
                      << std::fixed << std::setprecision(1) << r.hitRate << "%" << std::endl;

            if (pn == "HotKey80/20") {
                if (p == "LRU") lruHot = r.hits;
                else if (p == "LFU") lfuHot = r.hits;
                else if (p == "FIFO") fifoHot = r.hits;
            }
        }
        std::cout << std::string(42, '-') << std::endl;
    }

    assert(lruHot >= fifoHot && "LRU should beat FIFO on skewed hot-key traffic");
    assert(lfuHot >= fifoHot && "LFU should beat FIFO on skewed hot-key traffic");
    std::cout << "\n[ok] relational invariants hold on hot-key workload" << std::endl;

    return 0;
}
