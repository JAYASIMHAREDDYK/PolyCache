#include "src/protocol/resp.hpp"
#include <charconv>
#include <sstream>
#include <iostream>

namespace polycache::protocol {

namespace {

std::optional<size_t> find_crlf(std::string_view sv, size_t from = 0) {
    size_t pos = sv.find("\r\n", from);
    if (pos == std::string_view::npos) return std::nullopt;
    return pos;
}

std::optional<int64_t> parse_int64(std::string_view sv) {
    if (sv.empty()) return std::nullopt;
    int64_t val = 0;
    auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), val);
    if (ec != std::errc() || ptr != sv.data() + sv.size()) {
        return std::nullopt;
    }
    return val;
}

} // namespace

ParseStatus RespParser::parse(std::string_view buffer, RespValue& out_value, size_t& consumed_bytes) {
    if (buffer.empty()) {
        return ParseStatus::Incomplete;
    }
    if (buffer.size() > max_request_size_) {
        return ParseStatus::Error;
    }

    size_t offset = 0;
    char prefix = buffer[0];

    // If prefix is not one of RESP standard prefixes, support inline command format (e.g. "PING\r\n")
    if (prefix != '+' && prefix != '-' && prefix != ':' && prefix != '$' && prefix != '*') {
        ParseStatus status = parse_inline(buffer, offset, out_value);
        if (status == ParseStatus::Ok) {
            consumed_bytes = offset;
        }
        return status;
    }

    ParseStatus status = parse_internal(buffer, offset, out_value);
    if (status == ParseStatus::Ok) {
        consumed_bytes = offset;
    }
    return status;
}

ParseStatus RespParser::parse_inline(std::string_view buffer, size_t& offset, RespValue& out_value) {
    auto crlf = find_crlf(buffer, offset);
    if (!crlf.has_value()) {
        return ParseStatus::Incomplete;
    }

    std::string_view line = buffer.substr(offset, *crlf - offset);
    offset = *crlf + 2;

    std::vector<RespValue> tokens;
    size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
            i++;
        }
        if (i >= line.size()) break;

        size_t start = i;
        if (line[i] == '"') {
            i++;
            std::string token;
            while (i < line.size() && line[i] != '"') {
                if (line[i] == '\\' && i + 1 < line.size()) {
                    token.push_back(line[i + 1]);
                    i += 2;
                } else {
                    token.push_back(line[i]);
                    i++;
                }
            }
            if (i < line.size() && line[i] == '"') i++;
            tokens.push_back(RespValue::make_bulk(std::move(token)));
        } else {
            while (i < line.size() && line[i] != ' ' && line[i] != '\t') {
                i++;
            }
            tokens.push_back(RespValue::make_bulk(std::string(line.substr(start, i - start))));
        }
    }

    if (tokens.empty()) {
        return ParseStatus::Error;
    }

    out_value = RespValue::make_array(std::move(tokens));
    return ParseStatus::Ok;
}

ParseStatus RespParser::parse_internal(std::string_view buffer, size_t& offset, RespValue& out_value) {
    if (offset >= buffer.size()) {
        return ParseStatus::Incomplete;
    }

    char type_char = buffer[offset];
    offset += 1;

    switch (type_char) {
        case '+': { // Simple string
            auto crlf = find_crlf(buffer, offset);
            if (!crlf.has_value()) return ParseStatus::Incomplete;
            out_value = RespValue::make_simple(std::string(buffer.substr(offset, *crlf - offset)));
            offset = *crlf + 2;
            return ParseStatus::Ok;
        }
        case '-': { // Error
            auto crlf = find_crlf(buffer, offset);
            if (!crlf.has_value()) return ParseStatus::Incomplete;
            out_value = RespValue::make_error(std::string(buffer.substr(offset, *crlf - offset)));
            offset = *crlf + 2;
            return ParseStatus::Ok;
        }
        case ':': { // Integer
            auto crlf = find_crlf(buffer, offset);
            if (!crlf.has_value()) return ParseStatus::Incomplete;
            auto parsed = parse_int64(buffer.substr(offset, *crlf - offset));
            if (!parsed.has_value()) return ParseStatus::Error;
            out_value = RespValue::make_integer(*parsed);
            offset = *crlf + 2;
            return ParseStatus::Ok;
        }
        case '$': { // Bulk string
            auto crlf = find_crlf(buffer, offset);
            if (!crlf.has_value()) return ParseStatus::Incomplete;
            auto len_opt = parse_int64(buffer.substr(offset, *crlf - offset));
            if (!len_opt.has_value()) return ParseStatus::Error;
            int64_t len = *len_opt;
            offset = *crlf + 2;

            if (len == -1) {
                out_value = RespValue::make_null();
                return ParseStatus::Ok;
            }
            if (len < 0 || static_cast<size_t>(len) > max_request_size_) {
                return ParseStatus::Error;
            }

            size_t ulen = static_cast<size_t>(len);
            if (offset + ulen + 2 > buffer.size()) {
                return ParseStatus::Incomplete;
            }
            if (buffer.substr(offset + ulen, 2) != "\r\n") {
                return ParseStatus::Error;
            }

            out_value = RespValue::make_bulk(std::string(buffer.substr(offset, ulen)));
            offset += ulen + 2;
            return ParseStatus::Ok;
        }
        case '*': { // Array
            auto crlf = find_crlf(buffer, offset);
            if (!crlf.has_value()) return ParseStatus::Incomplete;
            auto count_opt = parse_int64(buffer.substr(offset, *crlf - offset));
            if (!count_opt.has_value()) return ParseStatus::Error;
            int64_t count = *count_opt;
            offset = *crlf + 2;

            if (count == -1) {
                out_value = RespValue::make_null();
                return ParseStatus::Ok;
            }
            if (count < 0 || count > 1024 * 1024) {
                return ParseStatus::Error;
            }

            std::vector<RespValue> elems;
            elems.reserve(count);
            for (int64_t i = 0; i < count; ++i) {
                RespValue elem;
                ParseStatus sub_status = parse_internal(buffer, offset, elem);
                if (sub_status != ParseStatus::Ok) {
                    return sub_status;
                }
                elems.push_back(std::move(elem));
            }
            out_value = RespValue::make_array(std::move(elems));
            return ParseStatus::Ok;
        }
        default:
            return ParseStatus::Error;
    }
}

std::string RespSerializer::serialize(const RespValue& value) {
    switch (value.type) {
        case RespType::SimpleString:
            return "+" + value.str_val + "\r\n";
        case RespType::Error:
            return "-" + value.str_val + "\r\n";
        case RespType::Integer:
            return ":" + std::to_string(value.int_val) + "\r\n";
        case RespType::BulkString:
            return "$" + std::to_string(value.str_val.size()) + "\r\n" + value.str_val + "\r\n";
        case RespType::Null:
            return "$-1\r\n";
        case RespType::Array: {
            std::string out = "*" + std::to_string(value.elements.size()) + "\r\n";
            for (const auto& elem : value.elements) {
                out += serialize(elem);
            }
            return out;
        }
    }
    return "-ERR unknown serialization type\r\n";
}

std::string RespSerializer::ok() {
    return "+OK\r\n";
}

std::string RespSerializer::pong(const std::string& msg) {
    if (msg.empty()) {
        return "+PONG\r\n";
    }
    return bulk_string(msg);
}

std::string RespSerializer::error(const std::string& err_msg) {
    if (err_msg.rfind("ERR", 0) == 0 || err_msg.rfind("WRONGTYPE", 0) == 0) {
        return "-" + err_msg + "\r\n";
    }
    return "-ERR " + err_msg + "\r\n";
}

std::string RespSerializer::integer(int64_t n) {
    return ":" + std::to_string(n) + "\r\n";
}

std::string RespSerializer::bulk_string(std::string_view s) {
    return "$" + std::to_string(s.size()) + "\r\n" + std::string(s) + "\r\n";
}

std::string RespSerializer::null_bulk() {
    return "$-1\r\n";
}

std::string RespSerializer::null_array() {
    return "*-1\r\n";
}

std::string RespSerializer::string_array(const std::vector<std::string>& strings) {
    std::string out = "*" + std::to_string(strings.size()) + "\r\n";
    for (const auto& s : strings) {
        out += "$" + std::to_string(s.size()) + "\r\n" + s + "\r\n";
    }
    return out;
}

std::string RespSerializer::command(const std::vector<std::string>& args) {
    return string_array(args);
}

} // namespace polycache::protocol
