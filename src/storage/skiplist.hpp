#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <random>
#include <cstdint>
#include <cstddef>
#include <optional>

namespace polycache::storage {

constexpr int SKIPLIST_MAXLEVEL = 32;
constexpr double SKIPLIST_P = 0.25;

struct SkipListLevel {
    struct SkipListNode* forward{nullptr};
    uint32_t span{0};
};

struct SkipListNode {
    std::string member;
    double score{0.0};
    SkipListNode* backward{nullptr};
    std::vector<SkipListLevel> level;

    SkipListNode(int level_count, double s, std::string m)
        : member(std::move(m)), score(s), level(level_count) {}
};

class SkipList {
public:
    SkipList();
    ~SkipList();

    SkipList(const SkipList&) = delete;
    SkipList& operator=(const SkipList&) = delete;
    SkipList(SkipList&& other) noexcept;
    SkipList& operator=(SkipList&& other) noexcept;

    // Returns true if new member added, false if score was updated
    bool insert(const std::string& member, double score);

    // Removes member, returns true if found and removed
    bool remove(const std::string& member);

    std::optional<double> get_score(const std::string& member) const;

    size_t size() const { return length_; }
    bool empty() const { return length_ == 0; }

    struct RangeResult {
        std::string member;
        double score;
    };

    std::vector<RangeResult> range_by_score(
        double min_score, double max_score,
        int offset = 0, int count = -1,
        bool with_min_exclusive = false, bool with_max_exclusive = false) const;

    size_t memory_overhead() const;

private:
    int random_level();
    void delete_node(SkipListNode* x, SkipListNode** update);

    SkipListNode* header_{nullptr};
    SkipListNode* tail_{nullptr};
    size_t length_{0};
    int level_{1};
    std::unordered_map<std::string, double> dict_;
    std::mt19937 rng_{42};
};

} // namespace polycache::storage
