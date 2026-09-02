# C++ Order Book

A single-instrument limit order book matching engine in C++23, built as a
latency-engineering study. The objective of this project isn't the matching logic — that was done [here](https://github.com/williamleonards/OrderMatchingEngine). Instead, it is to observe the **tail latencies**
once the logic is correct, and how much of that tail is the data structures
versus the operating system underneath them.

The repo is organized as a series of implementations (`v1_baseline`,
`v2_*`, `v3_*`) that expose an identical interface and consume an identical
binary feed, so any two of them can be compared on the same input under the
same benchmark harness.

---

## Status

| Version | Focus | State |
| --- | --- | --- |
| `v1_baseline` | Correct engine on STL containers (`std::set` + `std::unordered_map`) | **Done** |
| `v2_*` | OS-level jitter: core pinning, memory locking, preallocation | Planned |
| `v3_*` | Data-structure-level: custom open-addressed hash map, cache-aware B-tree | Planned |

---

## The problem

The engine consumes a stream of fixed-size binary requests and emits a stream of
responses. Three request types:

| Request | Fields | Semantics |
| --- | --- | --- |
| `ORDER` | `order_id, side, price, amt` | Match against the opposite book while the price crosses; rest the residual |
| `PARTIAL_CANCEL` | `order_id, side, amt` | Reduce a resting order by `amt`; remove it if depleted |
| `FULL_CANCEL` | `order_id, side` | Remove a resting order outright |

And four response types: `TRADE`, `ACK`, `CANCELLATION` (with a status of
`SUCCESS` / `INVALID_AMT` / `ID_NOT_FOUND`), and `ERROR`.

Matching is **price-time priority**: best price first, and within a price level,
lowest `order_id` first — which works as a time proxy because the exchange issues
IDs monotonically. Trades execute at the *resting* order's price, not the
incoming one.

Both `Request` and `Response` are unions over a common header, so the whole
protocol is one fixed-size POD record (48 bytes for `Request`) with no
delimiters, no allocation, and no parsing on the hot path. Reading the type tag
out of an inactive union member is technically UB in ISO C++ but is a documented
GCC/Clang extension, which this project relies on deliberately.

See [common/include/schema.h](common/include/schema.h).


The engine is templated on its input and output stream types
(`OrderBook<InStream, OutStream>`), so swapping a `std::vector` feed for a ring
buffer or a socket is a type parameter, not a rewrite. The benchmark classes are
templated the same way over the engine itself, which is what will let v1, v2 and
v3 share one harness.

---

## Roadmap

### v1 — baseline

Standard DSA-style implementation with optimal runtime-complexities.

### v2 — take the OS out of the picture

Same data structures, different relationship with the kernel. The goal is to
remove sources of jitter that have nothing to do with the algorithm.

- **Core pinning** (`sched_setaffinity`) so the hot thread stops migrating and
  arriving on a cold L1/L2 with an empty branch predictor. Paired with
  `isolcpus` / `nohz_full` on the benchmark machine so nothing else is scheduled
  there.
- **Memory locking** (`mlockall(MCL_CURRENT | MCL_FUTURE)`) to pin the working
  set resident and eliminate minor faults and any possibility of a swap.
- **Warming up of data structures** before the actual hot path to eliminate minor page faults.
- **Candidate follow-ups (TBD)**: transparent/explicit huge pages to cut TLB pressure,
  and a warm-up pass over the structures before measurement begins.

Expected effect: the P99.9+ range should collapse. The median should barely
move — which is itself the result worth reporting.


### v3 — getting past the STL

The containers themselves, rebuilt for this access pattern.
- **Preallocation** — a bump/pool allocator sized for the maximum expected book
  depth, touched at startup so every page is faulted in and every TLB entry is
  warm before the first request is timed. No `malloc` on the hot path at all.
- **Custom hash map** for `order_id → resting order`. Open addressing with
  linear probing over a single flat array, power-of-two capacity, sized once at
  startup. One cache line per probe instead of a pointer chase per node, and no
  rehashing mid-session.
- **Custom B-tree (TBD)** for the price-ordered book. Nodes sized to a cache line (or
  a small multiple), keys packed contiguously, so one cache miss reads many keys
  instead of one. Far shallower than a red-black tree at the same element count,
  and the best-price lookup stays O(1) with a cached leftmost pointer.

---

## v1: baseline design

Two ordered sets and two hash maps:

```cpp
std::set<BookEntry, MaxPrice> buy_heap_;    // best bid at begin()
std::set<BookEntry, MinPrice> sell_heap_;   // best ask at begin()
std::unordered_map<uint64_t, set<BookEntry, MaxPrice>::iterator> buy_orders_;
std::unordered_map<uint64_t, set<BookEntry, MinPrice>::iterator> sell_orders_;
```

The key decision is storing **iterators**, not keys, in the ID maps.
`std::set` iterators are stable across insertion and erasure of other elements,
so a cancel is a hash lookup plus a direct node erase — amortized O(1) — instead
of a lookup followed by an O(log n) search by key. `BookEntry::amt` is `mutable`
so a partial cancel can decrement the resting quantity in place without an
erase/reinsert, since `amt` doesn't participate in the ordering.

Matching walks `begin()` of the opposite book, which is the best price by
construction, and stops on the first entry that doesn't cross.

### What this buys, and what it costs

Every operation is O(log n) or better, so the *algorithmic* work is already
where it should be. What's left is entirely constant factors, and they are
large:

- **`std::set` is a red-black tree** — one heap allocation per resting order,
  nodes scattered across the heap, and a pointer chase per level of depth on
  every match. Best-price lookup is `begin()`, which is cheap, but insertion
  walks and rebalances.
- **`std::unordered_map` is a bucket array of linked lists** — a pointer chase
  per lookup and a rehash whenever the load factor trips.
- **Neither container is told anything up front.** Every allocation is a
  potential `malloc` slow path, and every first touch of a fresh page is a page
  fault.

That's the motivation for v2 and v3.

---

## Benchmarking methodology

`bench/percentile.h` times each individual request rather than the batch, since
a mean hides exactly the behavior worth studying.

- Timestamps come from `rdtsc` / `rdtscp`, reading the invariant TSC, which
  ticks at a fixed frequency independent of the core's current P-state.
- The start capture is preceded by `lfence` so prior instructions have retired
  before the counter is read; the end capture uses `rdtscp` (which serializes on
  prior instructions itself) followed by `lfence` so later instructions can't
  begin early.
- A `COMPILER_BARRIER()` (`asm volatile("" ::: "memory")`) brackets both, so the
  compiler can't hoist work across the measurement.
- The TSC frequency is calibrated at startup against `steady_clock` over a
  100 ms sleep, after a 10 ms warm-up, and cycles are converted to nanoseconds
  with that ratio.

---

## Results

### v1: baseline

1,000,000 requests per feed. Intel Core i9-14900HX, GCC 13.3, `-O2`, generic
`x86-64`. **Stock desktop conditions**: no core pinning, no isolated CPUs, no
`mlockall`, frequency scaling and other tenants left on.

| Feed | P50 | P90 | P99 | P99.9 | P99.99 | Max |
| --- | --- | --- | --- | --- | --- | --- |
| `typical` | 81 ns | 124 ns | 771 ns | 2.3 µs | 9.2 µs | 290 µs |
| `volatile` | 69 ns | 129 ns | 800 ns | 2.6 µs | 8.9 µs | 422 µs |
| `flash_crash` | 79 ns | 131 ns | 759 ns | 2.5 µs | 9.3 µs | 99 µs |

As expected, the median is fine with atrocious tail latencies. Looking across 
different feeds, it appears that the tail does not come from market structure or matching logic. It's coming from everything underneath: allocator slow paths,
page faults, cache misses on tree traversal, context switches, etc.

That's the thesis for the rest of the project — the remaining versions attack
the tail, not the median.

### v2: isolating the os
Inserting a breakpoint just before the hot paths enables `perf` to analyze what's
happening:

```bash
william-sumendap@william-sumendap-Legion-Pro-5-16IRX9:~/Documents/Order-Book$ sudo perf stat -e faults,minor-faults,major-faults,cs -p 62074

 Performance counter stats for process id '62074':

            14,447      faults                                                                
            14,447      minor-faults                                                          
                 0      major-faults                                                          
                 2      cs                                                                    

       3.002248206 seconds time elapsed
```

By locking memory pages, preventing freed pages of the heap from being reclaimed, and warming up our data structures, we can eliminate the
page faults:

```bash
william-sumendap@william-sumendap-Legion-Pro-5-16IRX9:~/Documents/Order-Book$ sudo perf stat -e faults,minor-faults,major-faults,cs -p 100930

 Performance counter stats for process id '100930':

                 1      faults                                                                
                 1      minor-faults                                                          
                 0      major-faults                                                          
                 2      cs                                                                    

       3.002519446 seconds time elapsed
```

That alone reduces our P99+ massively:

```bash
william-sumendap@william-sumendap-Legion-Pro-5-16IRX9:~/Documents/Order-Book$ taskset -c 3 ./build/v1_baseline/v1_baseline data/typical.bin
Setup and warmup complete. PID: 100930
Press Enter to begin 1 million hot path requests...
Latency distributions:
P50 = 76.47424039210482 ns
P90 = 110.78430500045455 ns
P99 = 253.81180324731005 ns
P99.9 = 414.20102093453534 ns
P99.99 = 3428.5262152006344 ns
MAX = 25297.265347435885 ns
Calibrated tsc frequency was 2.4191152347699467 cycles per ns
```

---

## Building

Requires CMake ≥ 3.20 and a C++23 compiler (developed on GCC 13.3).

```bash
cmake -S . -B build
cmake --build build -j
```

Warnings are on and meant to stay clean: `-Wpedantic -Wall -Wextra -Wconversion`.

## Running

```bash
./build/v1_baseline/v1_baseline <feed.bin>
```

Currently defaults to vector in/out streams and the percentile benchmark. The
CLI is stubbed for `[INPUT_MODE] [OUTPUT_MODE] [BENCH_TYPE]` selection but
rejects extra arguments until those are wired up — see
[v1_baseline/src/main.cpp](v1_baseline/src/main.cpp).

## Testing

```bash
cd build && ctest --output-on-failure
```

The suite is a hand-written scenario in
[v1_baseline/test/test.h](v1_baseline/test/test.h) exercising crossing orders,
partial fills, partial cancels and full cancels, with the resulting responses
and both books dumped for inspection.


## Test data

Feeds are generated, not committed (see [.gitignore](.gitignore)) — only the
small `handwritten.bin` and the generator sources are in the repo.

```bash
g++ -std=c++23 -I common/include -O2 -o /tmp/gen_feed v1_baseline/test/data/gen_feed.cpp
/tmp/gen_feed typical     v1_baseline/test/data/typical     1000000 1
/tmp/gen_feed volatile    v1_baseline/test/data/volatile    1000000 2
/tmp/gen_feed flash_crash v1_baseline/test/data/flash_crash 1000000 3
```

Each run writes a `.bin` (the feed the engine reads) and a human-readable `.txt`
twin whose body lines correspond one-to-one with the binary records.

The generator runs a lightweight price-time-priority book of its own — not to
decide the engine's output, but to keep the resting book bounded and to aim
cancels at orders that are actually resting. Price dynamics are
Ornstein-Uhlenbeck over a normalized trading day, so the daily range is
invariant to request count: the same regime produces the same price path at
100k requests as at 10M. Deterministic given a seed.

| Scenario | Character |
| --- | --- |
| `typical` | Calm, mean-reverting mid, tight spread, balanced flow |
| `volatile` | Trending mid, wide spread, fat-tailed sizes, directional bursts |
| `flash_crash` | Calm → one-sided sell cascade with liquidity withdrawal → partial recovery |

The `.bin` format is native x86-64 little-endian and is not portable across
ABIs.

---

## Known limitations

- `VectorOutStream` silently **drops** responses once it hits its preallocated
  capacity, to avoid a reallocation mid-measurement. Fine for benchmarking,
  wrong for anything real.
- The engine assumes `order_id` is globally unique and never validates it.
- Single instrument, single thread, no persistence, no recovery.
- Union type-punning relies on a GCC/Clang extension.
- Latency numbers above are from a noisy desktop (for now); treat them as a shape, not an
  absolute.
