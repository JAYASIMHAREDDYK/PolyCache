#include "src/protocol/resp.hpp"
#include <cassert>
#include <iostream>
#include <string>

void test_resp_parser_basic() {
    using namespace polycache::protocol;
    RespParser parser;

    // 1. Simple String
    {
        RespValue val;
        size_t consumed = 0;
        assert(parser.parse("+OK\r\n", val, consumed) == ParseStatus::Ok);
        assert(consumed == 5);
        assert(val.type == RespType::SimpleString);
        assert(val.str_val == "OK");
    }

    // 2. Integer
    {
        RespValue val;
        size_t consumed = 0;
        assert(parser.parse(":42\r\n", val, consumed) == ParseStatus::Ok);
        assert(consumed == 5);
        assert(val.type == RespType::Integer);
        assert(val.int_val == 42);
    }

    // 3. Bulk String
    {
        RespValue val;
        size_t consumed = 0;
        assert(parser.parse("$5\r\nhello\r\n", val, consumed) == ParseStatus::Ok);
        assert(consumed == 11);
        assert(val.type == RespType::BulkString);
        assert(val.str_val == "hello");
    }

    // 4. Null Bulk String
    {
        RespValue val;
        size_t consumed = 0;
        assert(parser.parse("$-1\r\n", val, consumed) == ParseStatus::Ok);
        assert(consumed == 5);
        assert(val.type == RespType::Null);
    }

    // 5. Array: *2\r\n$3\r\nGET\r\n$3\r\nkey\r\n
    {
        RespValue val;
        size_t consumed = 0;
        std::string raw = "*2\r\n$3\r\nGET\r\n$3\r\nkey\r\n";
        assert(parser.parse(raw, val, consumed) == ParseStatus::Ok);
        assert(consumed == raw.size());
        assert(val.type == RespType::Array);
        assert(val.elements.size() == 2);
        assert(val.elements[0].str_val == "GET");
        assert(val.elements[1].str_val == "key");
    }

    // 6. Inline Command
    {
        RespValue val;
        size_t consumed = 0;
        assert(parser.parse("PING\r\n", val, consumed) == ParseStatus::Ok);
        assert(consumed == 6);
        assert(val.type == RespType::Array);
        assert(val.elements[0].str_val == "PING");
    }

    std::cout << "  [PASS] test_resp_parser_basic\n";
}

void test_resp_streaming_and_pipelining() {
    using namespace polycache::protocol;
    RespParser parser;

    // Fragmented packet test
    std::string full = "*2\r\n$3\r\nGET\r\n$4\r\ntest\r\n";
    RespValue val;
    size_t consumed = 0;

    // Partial input: not enough bytes
    assert(parser.parse(full.substr(0, 5), val, consumed) == ParseStatus::Incomplete);
    assert(parser.parse(full.substr(0, 12), val, consumed) == ParseStatus::Incomplete);

    // Full packet arrives
    assert(parser.parse(full, val, consumed) == ParseStatus::Ok);
    assert(consumed == full.size());
    assert(val.elements.size() == 2);
    assert(val.elements[1].str_val == "test");

    // Pipelining test: two commands concatenated
    std::string pipelined = "*1\r\n$4\r\nPING\r\n*1\r\n$4\r\nPING\r\n";
    RespValue cmd1, cmd2;
    size_t c1 = 0, c2 = 0;

    assert(parser.parse(pipelined, cmd1, c1) == ParseStatus::Ok);
    assert(c1 == 14);
    assert(parser.parse(pipelined.substr(c1), cmd2, c2) == ParseStatus::Ok);
    assert(c2 == 14);

    // Incremental prefix test
    std::string exact = "*3\r\n$3\r\nSET\r\n$12\r\nfragment_key\r\n$14\r\nfragment_value\r\n";
    for (size_t len = 1; len < exact.size(); ++len) {
        RespValue val_sub;
        size_t c_sub = 0;
        ParseStatus st = parser.parse(exact.substr(0, len), val_sub, c_sub);
        if (st != ParseStatus::Incomplete) {
            std::cerr << "Failed at prefix len " << len << ": '" << exact.substr(0, len) << "' -> status=" << (int)st << "\n";
            assert(false);
        }
    }

    std::cout << "  [PASS] test_resp_streaming_and_pipelining\n";
}
