#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <string_view>
#include <optional>

namespace polycache::protocol {

enum class RespType {
    SimpleString,
    Error,
    Integer,
    BulkString,
    Array,
    Null
};

struct RespValue {
    RespType type{RespType::Null};
    std::string str_val;
    int64_t int_val{0};
    std::vector<RespValue> elements;

    static RespValue make_simple(std::string s) {
        RespValue v;
        v.type = RespType::SimpleString;
        v.str_val = std::move(s);
        return v;
    }

    static RespValue make_error(std::string err) {
        RespValue v;
        v.type = RespType::Error;
        v.str_val = std::move(err);
        return v;
    }

    static RespValue make_integer(int64_t n) {
        RespValue v;
        v.type = RespType::Integer;
        v.int_val = n;
        return v;
    }

    static RespValue make_bulk(std::string s) {
        RespValue v;
        v.type = RespType::BulkString;
        v.str_val = std::move(s);
        return v;
    }

    static RespValue make_null() {
        RespValue v;
        v.type = RespType::Null;
        return v;
    }

    static RespValue make_array(std::vector<RespValue> elems) {
        RespValue v;
        v.type = RespType::Array;
        v.elements = std::move(elems);
        return v;
    }
};

enum class ParseStatus {
    Ok,
    Incomplete,
    Error
};

class RespParser {
public:
    explicit RespParser(size_t max_request_size = 64 * 1024 * 1024)
        : max_request_size_(max_request_size) {}

    // Parses a single RESP value from buffer starting at 0.
    // On Ok: out_value is populated, consumed_bytes is the number of bytes to remove.
    ParseStatus parse(std::string_view buffer, RespValue& out_value, size_t& consumed_bytes);

private:
    ParseStatus parse_internal(std::string_view buffer, size_t& offset, RespValue& out_value);
    ParseStatus parse_inline(std::string_view buffer, size_t& offset, RespValue& out_value);

    size_t max_request_size_;
};

class RespSerializer {
public:
    static std::string serialize(const RespValue& value);
    static std::string ok();
    static std::string pong(const std::string& msg = "");
    static std::string error(const std::string& err_msg);
    static std::string integer(int64_t n);
    static std::string bulk_string(std::string_view s);
    static std::string null_bulk();
    static std::string null_array();
    static std::string string_array(const std::vector<std::string>& strings);
    static std::string command(const std::vector<std::string>& args);
};

} // namespace polycache::protocol
