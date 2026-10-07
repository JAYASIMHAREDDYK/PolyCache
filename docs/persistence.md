# PolyCache Persistence Specification

## 1. Append-Only File (AOF) Architecture

PolyCache logs all mutating commands (`SET`, `DEL`, `INCR`, `DECR`, `EXPIRE`, `ZADD`, `ZREM`, `FLUSHDB`) in RESP2 array format directly to disk (`appendonly.aof`).

### Fsync Policies
- `always`: Invokes system `fsync()` after every write call. Maximum durability, bounded by disk I/O latency.
- `everysec`: Writes to OS file cache immediately; a timer thread or event loop tick flushes and calls `fsync()` once per second. Optimal balance between throughput and safety (at most 1 second of data loss upon power failure).
- `no`: Relies on kernel page cache flushing. Maximum throughput.

## 2. Background AOF Rewrite (`BGREWRITEAOF`)

As write operations accumulate, the AOF log file grows linearly. PolyCache implements background rewriting to produce a minimal, compacted dataset representation.

```
       Parent Process (Event Loop)                  Child / Worker
   [ Serving Client Requests ]                    [ Snapshot Writer ]
                 |                                         |
                 +---- fork() (Linux COW) ---------------->|
                 |                                         | Iterates keyspace snapshot
   New mutations |                                         | Writes minimal commands
   appended to   |                                         | to temp-rewrite.aof
   rewrite_buf_  |                                         |
                 |                                         v
                 |<--- waitpid() child exit <--------------+
                 |
   1. Append rewrite_buf_ to temp-rewrite.aof
   2. Atomically rename(temp-rewrite.aof, appendonly.aof)
   3. Close and reopen file descriptor
```

### Linux Copy-on-Write (COW) Implementation
1. When `BGREWRITEAOF` is invoked, the server calls `fork()`.
2. Under Linux virtual memory semantics, the child process shares physical memory pages with the parent process. The child gets a point-in-time snapshot with zero memory duplication overhead at invocation time.
3. The child iterates over all resident non-expired keys and writes clean reconstructing statements:
   - Strings: `SET key val` + `EXPIRE key ttl`
   - Sorted sets: `ZADD key score member...` + `EXPIRE key ttl`
4. The parent continues serving client reads and writes uninterrupted. New write operations are written to both the existing AOF and accumulated in `rewrite_buffer_`.
5. When the child process terminates (`exit 0`), the parent detects this via `waitpid(..., WNOHANG)` in the event loop tick.
6. The parent appends `rewrite_buffer_` to `temp-rewrite.aof`, atomically renames the temporary file to `appendonly.aof`, reopens the descriptor, and clears the buffer.

### Windows Compatibility
On Windows (where `fork()` is unavailable), PolyCache runs an asynchronous worker thread that dumps a consistent snapshot, ensuring 100% feature and test compatibility across operating systems.

## 3. Crash Recovery
On server boot, PolyCache inspects the designated AOF path. If present, `AofManager::replay` streams and parses the log, executing each command sequentially into `Store` before opening the listener socket to incoming clients.
