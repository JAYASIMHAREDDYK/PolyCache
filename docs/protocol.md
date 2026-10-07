# PolyCache RESP2 Protocol Specification

PolyCache implements a useful subset of the Redis Serialization Protocol version 2 (RESP2).

## 1. Supported Wire Types

| RESP Type | Byte Prefix | Description | Example Wire Encoding |
|-----------|-------------|-------------|-----------------------|
| Simple String | `+` | Small status messages (no CRLF in message) | `+OK\r\n` |
| Error | `-` | Error message (e.g. `ERR`, `WRONGTYPE`) | `-ERR unknown command\r\n` |
| Integer | `:` | 64-bit signed integer | `:100\r\n` |
| Bulk String | `$` | Binary-safe strings with explicit length | `$5\r\nhello\r\n` |
| Null Bulk String | `$` | Null string | `$-1\r\n` |
| Array | `*` | Multi-element collection of RESP items | `*2\r\n$3\r\nGET\r\n$3\r\nkey\r\n` |
| Null Array | `*` | Null array representation | `*-1\r\n` |

## 2. Pipelining & Streaming Support
- **Pipelining**: Multiple commands can be packed into a single network packet or input buffer without waiting for intermediate replies.
- **Fragmentation**: Handles arbitrary network frame boundaries; incomplete frames remain in the connection buffer until subsequent TCP frames arrive.
- **Inline Format**: Supports simple space-delimited text commands (e.g. `PING\r\n`) for debugging via `telnet` / `nc`.

## 3. Command Surface Reference

### Strings & Keys
- `PING [message]`: Returns `+PONG\r\n` or the echoed message.
- `SET key value [EX seconds] [PX milliseconds]`: Stores a string value with optional TTL.
- `GET key`: Retrieves string value or null bulk string if missing/expired.
- `DEL key [key ...]`: Removes keys. Returns count of deleted keys.
- `EXISTS key [key ...]`: Checks if keys exist. Returns count of existing keys.

### Counters
- `INCR key`: Increments key by 1. Initializes to 1 if not present.
- `DECR key`: Decrements key by 1. Initializes to -1 if not present.

### Expiry
- `EXPIRE key seconds`: Sets time-to-live in seconds. Returns 1 on success, 0 if key not found.
- `TTL key`: Returns remaining TTL in seconds (-2 if key does not exist, -1 if key has no expiration).

### Sorted Sets (Skip List backed)
- `ZADD key score member [score member ...]`: Adds or updates members with scores. Returns number of newly added elements.
- `ZRANGEBYSCORE key min max [WITHSCORES] [LIMIT offset count]`: Returns members within score range.
- `ZREM key member [member ...]`: Removes members from sorted set. Returns count of removed items.

### Server & Persistence
- `INFO [section]`: Returns telemetry, memory, client count, hit rates, and latency stats.
- `DBSIZE`: Returns total keys in datastore.
- `FLUSHDB`: Clears all resident keys.
- `BGREWRITEAOF`: Triggers background Append-Only File rewrite.
- `LASTSAVE`: Returns UNIX timestamp of last successful save or rewrite.
