#include "src/persistence/aof.hpp"
#include "src/storage/store.hpp"
#include <cassert>
#include <iostream>
#include <filesystem>

void test_aof_append_and_replay() {
    using namespace polycache;

    const std::string test_aof = "test_appendonly.aof";
    std::filesystem::remove(test_aof);

    {
        persistence::AofManager aof(test_aof, persistence::AofFsync::Always);
        assert(aof.open());

        aof.append_command({"SET", "user:1", "alice"});
        aof.append_command({"SET", "user:2", "bob"});
        aof.append_command({"INCR", "counter"});
        aof.append_command({"INCR", "counter"});
        aof.append_command({"DEL", "user:2"});
        aof.close();
    }

    // Now replay into fresh store
    storage::Store replayed_store;
    persistence::AofManager aof2(test_aof, persistence::AofFsync::Always);
    size_t replayed_count = 0;
    assert(aof2.replay(replayed_store, &replayed_count));
    assert(replayed_count == 5);

    assert(replayed_store.get("user:1").has_value() && *replayed_store.get("user:1") == "alice");
    assert(!replayed_store.get("user:2").has_value());
    assert(replayed_store.get("counter").has_value() && *replayed_store.get("counter") == "2");

    std::filesystem::remove(test_aof);
    std::cout << "  [PASS] test_aof_append_and_replay\n";
}
