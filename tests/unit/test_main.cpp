#include <iostream>

void test_dict_basic();
void test_dict_incremental_rehashing();
void test_skiplist_basic();
void test_skiplist_lexicographical_tiebreaker();
void test_resp_parser_basic();
void test_resp_streaming_and_pipelining();
void test_store_lru_eviction();
void test_store_ttl_expiration();
void test_aof_append_and_replay();

int main() {
    std::cout << "========================================\n";
    std::cout << "    PolyCache Core Unit Test Suite     \n";
    std::cout << "========================================\n\n";

    std::cout << "[+] Running Dict Tests...\n";
    test_dict_basic();
    test_dict_incremental_rehashing();

    std::cout << "\n[+] Running SkipList Tests...\n";
    test_skiplist_basic();
    test_skiplist_lexicographical_tiebreaker();

    std::cout << "\n[+] Running RESP Protocol Tests...\n";
    test_resp_parser_basic();
    test_resp_streaming_and_pipelining();

    std::cout << "\n[+] Running Eviction & Expiry Tests...\n";
    test_store_lru_eviction();
    test_store_ttl_expiration();

    std::cout << "\n[+] Running AOF Persistence Tests...\n";
    test_aof_append_and_replay();

    std::cout << "\n========================================\n";
    std::cout << "    ALL UNIT TESTS PASSED SUCCESSFULLY! \n";
    std::cout << "========================================\n";
    return 0;
}
