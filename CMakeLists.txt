cmake_minimum_required(VERSION 3.16)
project(PolyCache VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC)
    add_compile_options(/W4 /O2 /permissive-)
else()
    add_compile_options(-Wall -Wextra -O2)
endif()

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# PolyCache core static library
set(CORE_SOURCES
    src/net/socket.cpp
    src/net/event_loop.cpp
    src/protocol/resp.cpp
    src/storage/skiplist.cpp
    src/storage/store.cpp
    src/eviction/eviction.cpp
    src/persistence/aof.cpp
    src/metrics/metrics.cpp
    src/command/command.cpp
    src/server/server.cpp
)

add_library(polycache_core STATIC ${CORE_SOURCES})

if(WIN32)
    target_link_libraries(polycache_core PUBLIC ws2_32)
elseif(UNIX)
    target_link_libraries(polycache_core PUBLIC pthread)
endif()

# PolyCache server executable
add_executable(polycache-server src/main.cpp)
target_link_libraries(polycache-server PRIVATE polycache_core)

add_executable(test_server src/main.cpp)
target_link_libraries(test_server PRIVATE polycache_core)

# Unit test suite
enable_testing()

add_executable(test_runner
    tests/unit/test_dict.cpp
    tests/unit/test_skiplist.cpp
    tests/unit/test_resp.cpp
    tests/unit/test_eviction.cpp
    tests/unit/test_aof.cpp
    tests/unit/test_main.cpp
)
target_link_libraries(test_runner PRIVATE polycache_core)

add_test(NAME AllUnitTests COMMAND test_runner)
