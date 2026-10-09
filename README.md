# PolyCache

Fixed-capacity key-value cache in C++17 with swappable eviction policies (LRU, LFU, FIFO). Built as a single-file implementation to work through the Strategy pattern applied to a real data structure problem — specifically, how to make eviction logic interchangeable without the cache knowing which policy it's using, and how each policy's data structure choice follows from its O(1) constraint.

## Build / Run

```
g++ -std=c++17 -O2 -o cache cache.cpp
./cache
```

Single file, no dependencies beyond the standard library.

## Design

### Strategy pattern

`Cache` holds a `std::unique_ptr<EvictionPolicy>` and calls `onAccess`, `onInsert`, `evict`, `onRemove` — it never branches on policy type. Policies are injected via a factory function (`createPolicy("LRU")`). Adding a new policy means writing one class; `Cache` doesn't change.

### Policy hierarchy

LRU and FIFO share identical bookkeeping (ordered list + position map), differing only in whether `onAccess` moves the key to the front. This is extracted into `OrderedListPolicy`; `FIFOPolicy` is a one-liner override (`onAccess` does nothing) and `LRUPolicy` adds the move-to-front logic.

Convention: front = most recently inserted/accessed, back = eviction candidate. Both policies inherit the same `evict()` (pop from back) and `onInsert()` (push to front). This matters because mixing conventions (e.g. FIFO's "push to back" with LRU's "move to front") silently breaks LRU's eviction order without any obvious failure at the unit-test level — it only shows up as degraded hit rates under load.

### LFU's frequency-bucket trick

Naive LFU scans all keys for the minimum frequency — O(n). The O(1) approach: group keys into lists indexed by frequency (`buckets[freq] → list of keys`). Track `minFreq` as the lowest non-empty bucket. Eviction pops from `buckets[minFreq]` in O(1). On access, a key moves from `buckets[f]` to `buckets[f+1]`; if that empties bucket `f` and `f == minFreq`, bump `minFreq`.

One subtlety: `minFreq` is only correct because `Cache::put` always calls `onInsert` immediately after `evict`, which resets `minFreq` to 1. Calling `evict()` standalone (e.g. in a test harness) can leave `minFreq` stale. This is a protocol invariant between `Cache` and `LFUPolicy`, not enforced by the type system.

LFU uses `.at()` instead of `operator[]` for bucket/position lookups — if invariants are broken, it throws `std::out_of_range` instead of silently inserting empty entries.

## Benchmark Results

Capacity 50, 10,000 accesses, key range 0–199, fixed seed (42).

| Policy | Sequential | Random | Hot Key (80/20) |
|--------|-----------|--------|-----------------|
| LRU    | 0.0%      | 25.8%  | 84.1%           |
| LFU    | 0.0%      | 23.8%  | 84.2%           |
| FIFO   | 0.0%      | 25.2%  | 80.5%           |

**Sequential**: range (200) exceeds capacity (50) with no repetition within a cycle, so every access is a miss regardless of policy. All three score 0%.

**Random**: uniform distribution gives no exploitable pattern. All policies perform similarly — there's no "hot" subset to keep in cache.

**Hot Key**: 80% of accesses target 10 keys, 20% target the remaining 190. LRU and LFU both learn to retain the hot set; FIFO can't because it ignores usage. The code asserts `lru_hits >= fifo_hits` and `lfu_hits >= fifo_hits` on this workload as a relational invariant — this is a stronger check than eyeballing absolute numbers, and would have caught the front/back convention bug described above.

## Known Limitations

- **Not thread-safe.** No locking on `Cache` or any policy internals.
- **`assert(capacity > 0)` compiles to a no-op under `-DNDEBUG`.** The guard exists but vanishes in release builds that define `NDEBUG`. If this moved to production code, it should be a thrown exception.
- **`int` keys and values only.** Not templated — sufficient for the exercise but not reusable as a generic cache.
- **LFU `minFreq` invariant is a protocol, not a type constraint.** See the design section.

## What I'd add with more time

- Template on key/value types
- Thread-safe wrapper (or a lock-free LRU variant)
- TTL-based expiration as a fourth policy
- Larger-scale benchmarks with wall-clock timing, not just hit rates
