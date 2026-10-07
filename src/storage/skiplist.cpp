#include "src/storage/skiplist.hpp"
#include <algorithm>
#include <iostream>

namespace polycache::storage {

SkipList::SkipList() {
    header_ = new SkipListNode(SKIPLIST_MAXLEVEL, 0.0, "");
    tail_ = nullptr;
    length_ = 0;
    level_ = 1;
    rng_.seed(std::random_device{}());
}

SkipList::~SkipList() {
    SkipListNode* node = header_;
    while (node) {
        SkipListNode* next = node->level[0].forward;
        delete node;
        node = next;
    }
}

SkipList::SkipList(SkipList&& other) noexcept
    : header_(other.header_), tail_(other.tail_), length_(other.length_),
      level_(other.level_), dict_(std::move(other.dict_)), rng_(std::move(other.rng_)) {
    other.header_ = nullptr;
    other.tail_ = nullptr;
    other.length_ = 0;
    other.level_ = 1;
}

SkipList& SkipList::operator=(SkipList&& other) noexcept {
    if (this != &other) {
        // Free existing
        SkipListNode* node = header_;
        while (node) {
            SkipListNode* next = node->level[0].forward;
            delete node;
            node = next;
        }

        header_ = other.header_;
        tail_ = other.tail_;
        length_ = other.length_;
        level_ = other.level_;
        dict_ = std::move(other.dict_);
        rng_ = std::move(other.rng_);

        other.header_ = nullptr;
        other.tail_ = nullptr;
        other.length_ = 0;
        other.level_ = 1;
    }
    return *this;
}

int SkipList::random_level() {
    int lvl = 1;
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    while (dist(rng_) < SKIPLIST_P && lvl < SKIPLIST_MAXLEVEL) {
        lvl++;
    }
    return lvl;
}

std::optional<double> SkipList::get_score(const std::string& member) const {
    auto it = dict_.find(member);
    if (it != dict_.end()) {
        return it->second;
    }
    return std::nullopt;
}

bool SkipList::insert(const std::string& member, double score) {
    auto it = dict_.find(member);
    if (it != dict_.end()) {
        if (it->second == score) {
            return false; // Score unchanged
        }
        // Remove old position and reinsert with new score
        remove(member);
        insert(member, score);
        return false;
    }

    SkipListNode* update[SKIPLIST_MAXLEVEL];
    SkipListNode* x = header_;

    for (int i = level_ - 1; i >= 0; i--) {
        while (x->level[i].forward &&
               (x->level[i].forward->score < score ||
                (x->level[i].forward->score == score && x->level[i].forward->member < member))) {
            x = x->level[i].forward;
        }
        update[i] = x;
    }

    int lvl = random_level();
    if (lvl > level_) {
        for (int i = level_; i < lvl; i++) {
            update[i] = header_;
        }
        level_ = lvl;
    }

    x = new SkipListNode(lvl, score, member);
    for (int i = 0; i < lvl; i++) {
        x->level[i].forward = update[i]->level[i].forward;
        update[i]->level[i].forward = x;
    }

    x->backward = (update[0] == header_) ? nullptr : update[0];
    if (x->level[0].forward) {
        x->level[0].forward->backward = x;
    } else {
        tail_ = x;
    }

    length_++;
    dict_[member] = score;
    return true;
}

void SkipList::delete_node(SkipListNode* x, SkipListNode** update) {
    for (int i = 0; i < level_; i++) {
        if (update[i]->level[i].forward == x) {
            update[i]->level[i].forward = x->level[i].forward;
        }
    }
    if (x->level[0].forward) {
        x->level[0].forward->backward = x->backward;
    } else {
        tail_ = x->backward;
    }
    while (level_ > 1 && header_->level[level_ - 1].forward == nullptr) {
        level_--;
    }
    length_--;
}

bool SkipList::remove(const std::string& member) {
    auto it = dict_.find(member);
    if (it == dict_.end()) {
        return false;
    }
    double score = it->second;

    SkipListNode* update[SKIPLIST_MAXLEVEL];
    SkipListNode* x = header_;

    for (int i = level_ - 1; i >= 0; i--) {
        while (x->level[i].forward &&
               (x->level[i].forward->score < score ||
                (x->level[i].forward->score == score && x->level[i].forward->member < member))) {
            x = x->level[i].forward;
        }
        update[i] = x;
    }

    x = x->level[0].forward;
    if (x && x->score == score && x->member == member) {
        delete_node(x, update);
        dict_.erase(it);
        delete x;
        return true;
    }

    return false;
}

std::vector<SkipList::RangeResult> SkipList::range_by_score(
    double min_score, double max_score,
    int offset, int count,
    bool with_min_exclusive, bool with_max_exclusive) const {

    std::vector<RangeResult> results;
    if (min_score > max_score || empty()) return results;

    // Traverse to the first node with score >= min_score
    SkipListNode* x = header_;
    for (int i = level_ - 1; i >= 0; i--) {
        while (x->level[i].forward &&
               (with_min_exclusive
                    ? x->level[i].forward->score <= min_score
                    : x->level[i].forward->score < min_score)) {
            x = x->level[i].forward;
        }
    }

    x = x->level[0].forward;
    if (!x) return results;

    // Skip offset items
    while (x && offset > 0) {
        if (with_max_exclusive ? (x->score >= max_score) : (x->score > max_score)) {
            return results;
        }
        offset--;
        x = x->level[0].forward;
    }

    // Collect up to count items
    while (x) {
        if (with_max_exclusive) {
            if (x->score >= max_score) break;
        } else {
            if (x->score > max_score) break;
        }

        results.push_back({x->member, x->score});
        if (count > 0 && static_cast<int>(results.size()) >= count) {
            break;
        }
        x = x->level[0].forward;
    }

    return results;
}

size_t SkipList::memory_overhead() const {
    size_t bytes = sizeof(*this);
    bytes += dict_.size() * (sizeof(std::string) + sizeof(double) + 32); // rough map overhead
    bytes += length_ * (sizeof(SkipListNode) + sizeof(SkipListLevel) * 4); // avg levels
    return bytes;
}

} // namespace polycache::storage
