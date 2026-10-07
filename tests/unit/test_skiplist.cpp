#include "src/storage/skiplist.hpp"
#include <cassert>
#include <iostream>
#include <string>
#include <vector>

void test_skiplist_basic() {
    polycache::storage::SkipList sl;
    assert(sl.empty());
    assert(sl.size() == 0);

    assert(sl.insert("alice", 10.5) == true);
    assert(sl.insert("bob", 20.0) == true);
    assert(sl.insert("charlie", 5.0) == true);
    assert(sl.size() == 3);

    auto score = sl.get_score("alice");
    assert(score.has_value() && *score == 10.5);

    // Update score
    assert(sl.insert("alice", 25.0) == false);
    score = sl.get_score("alice");
    assert(score.has_value() && *score == 25.0);

    // Range by score
    // charlie: 5.0, bob: 20.0, alice: 25.0
    auto range = sl.range_by_score(0.0, 30.0);
    assert(range.size() == 3);
    assert(range[0].member == "charlie" && range[0].score == 5.0);
    assert(range[1].member == "bob" && range[1].score == 20.0);
    assert(range[2].member == "alice" && range[2].score == 25.0);

    // Sub-range
    auto sub = sl.range_by_score(10.0, 22.0);
    assert(sub.size() == 1);
    assert(sub[0].member == "bob");

    // Remove
    assert(sl.remove("bob") == true);
    assert(sl.size() == 2);
    assert(!sl.get_score("bob").has_value());
    assert(sl.remove("bob") == false);

    std::cout << "  [PASS] test_skiplist_basic\n";
}

void test_skiplist_lexicographical_tiebreaker() {
    polycache::storage::SkipList sl;
    // Identical scores
    sl.insert("zeta", 10.0);
    sl.insert("alpha", 10.0);
    sl.insert("beta", 10.0);

    auto range = sl.range_by_score(5.0, 15.0);
    assert(range.size() == 3);
    assert(range[0].member == "alpha");
    assert(range[1].member == "beta");
    assert(range[2].member == "zeta");

    std::cout << "  [PASS] test_skiplist_lexicographical_tiebreaker\n";
}
