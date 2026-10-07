# PolyCache Architecture

## 1. High-Level System Architecture

PolyCache is an asynchronous, in-memory, key-value datastore speaking a useful subset of the Redis RESP2 protocol.

```
       +-------------------------------------------------------------+
       |                       Client TCP Network                     |
       +-------------------------------------------------------------+
                                     |
                                     v
       +-------------------------------------------------------------+
       |               Non-Blocking Socket / Event Loop              |
       |     (Linux: epoll edge-triggered / Windows: WSAPoll)        |
       +-------------------------------------------------------------+
                                     |
                                     v
       +-------------------------------------------------------------+
       |                   Streaming RESP2 Parser                    |
       |            (Zero-copy, pipelining, chunking)                |
       +-------------------------------------------------------------+
                                     |
                                     v
       +-------------------------------------------------------------+
       |                     Command Dispatcher                      |
       |         (PING, SET, GET, DEL, INCR, ZADD, INFO, ...)        |
       +-------------------------------------------------------------+
                                     |
       +-----------------------------+-------------------------------+
       |                                                             |
       v                                                             v
+------------------------------------+             +----------------------------------+
|           Storage Engine           |             |       Persistence (AOF)          |
| - Custom Dict (Incremental Rehash) |             | - Fsync: always, everysec, no    |
| - SkipList Sorted Sets             |             | - Background rewrite             |
| - Memory Accounting & Eviction     |             |   (Linux fork COW / Win Worker)  |
| - Lazy & Active TTL Expiration     |             +----------------------------------+
+------------------------------------+
```

## 2. Event Loop & Network I/O
- **Single-Threaded Event Loop**: The datastore and client connection states are owned by a single-threaded event loop, eliminating locks and race conditions on core datastores.
- **Edge-Triggered epoll**: On Linux, client sockets are registered with `EPOLLET` (edge-triggered epoll) with `EPOLLIN` and dynamically enabled `EPOLLOUT`.
- **Non-blocking TCP**: Both listening socket and accepted sockets operate non-blocking (`O_NONBLOCK` / `FIONBIO`) with `TCP_NODELAY` and `SO_REUSEADDR`.
- **Buffer Management**: Per-client input and output buffers enable partial frame buffering, pipelining, and non-blocking draining.

## 3. Storage Layer & Incremental Rehashing
- **Custom Dict**: Dual-table architecture (`table_[0]` and `table_[1]`).
- **Incremental Migration**: When the load factor crosses 1.0, the hash table expands by $2\times$. Instead of a blocking $O(N)$ rehash pause, each dictionary operation moves bounded buckets from `table_[0]` to `table_[1]`.
- **Periodic Cron**: The server cron timer (`server_cron`) additionally executes 100 rehash steps every 100ms during idle periods.
- **Skip List**: Sorted sets are backed by a multi-level skip list ($p = 0.25$, max level = 32) combined with a hash table index for $O(1)$ member-to-score lookups and $O(\log N)$ range operations.

## 4. Eviction & Memory Model
- **Memory Tracking**: All memory consumed by keys, values, dictionary buckets, and skip list nodes is tracked in real-time.
- **Maxmemory**: Configurable via `--maxmemory` (e.g. 64MB, 1GB).
- **Approximate LRU/LFU**: Avoids the performance and space penalty of a global doubly-linked list. On memory pressure, PolyCache samples random resident keys, evaluates candidate idle times or logarithmic access frequencies, and evicts victims until resident memory is within bounds.

## 5. Persistence & Copy-on-Write
- **Append-Only File (AOF)**: Writes are logged sequentially in RESP2 format.
- **Fsync Modes**: Supports `always`, `everysec`, and `no`.
- **BGREWRITEAOF**: On Linux, uses `fork()` where the child process dumps an in-memory snapshot leveraging OS copy-on-write without blocking concurrent client commands. New writes during rewrite are captured in a memory buffer and atomically swapped.
