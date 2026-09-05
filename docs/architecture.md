# Architecture

```
 Browser
   |
   | HTTP / WebSocket
   v
 FastAPI web gateway (web/main.py)   <-- demo/visualization layer only
   |
   v
 In-process Python order book (demo only, not the benchmarked engine)


 ------------------------------------------------------------------
 Low-latency path (the actual C++ engineering artifact):
 ------------------------------------------------------------------

 TCP client / market-data source
   |
   v
 Feed handler (networking/tcp_server.cpp)
   - epoll, non-blocking sockets
   - fixed-width binary protocol (networking/protocol.hpp)
   - per-connection sequence-gap detection
   |
   v
 SPSC ring buffer (concurrency/spsc_queue.hpp)
   - lock-free, single-producer/single-consumer
   - cache-line-padded head/tail to avoid false sharing
   |
   v
 Matching thread
   |
   +--> Risk engine (risk/risk_engine.hpp)   pre-trade checks
   |
   +--> Order book (engine/order_book.hpp)   price-time priority match
   |
   +--> Trade callback / trade feed
```

## Why the web demo is a separate, simpler implementation

`web/main.py` re-implements a small order book directly in Python. This is
intentional, not an oversight: the point of the project is the C++ engine,
and the web layer exists only so a recruiter can click a button in a
browser. Presenting the Python demo book as "the HFT engine" would be
inaccurate — see the resume-readiness checklist in the project guide PDF.
If you want the web UI to drive the *real* C++ engine instead, the cleanest
path is to run `bin/gateway` (the TCP gateway) and have the FastAPI layer
act as a WebSocket<->TCP bridge, translating browser actions into the
binary wire protocol in `networking/protocol.hpp`. That bridge is a good
"next stage" project extension — see `docs/roadmap.md` equivalent in the
guide PDF's Section 22.

## Data-structure choices and why

| Choice | Reason |
|---|---|
| `std::map` for price levels | Ordered by construction, O(log n) insert, O(1) best-price via `begin()`. Not the fastest possible (a flat array of price buckets or an intrusive skip list would beat it) but correct and simple. |
| `std::deque` (not `std::vector`) per price level | FIFO pop-front is O(1). `vector::erase(begin())` is O(n) because it shifts every remaining element — this is called out explicitly as a bug in the original MVP and fixed here. |
| `std::unordered_map<id, Order*>` for cancellation | O(1) average lookup by order id. |
| `std::unique_ptr` ownership map, raw `Order*` elsewhere | Single owner, cheap pointers everywhere else; avoids `shared_ptr` refcount overhead on the hot path. |
| SPSC ring buffer between network and matching threads | One producer (network thread), one consumer (matching thread) — no CAS/lock needed, just relaxed/acquire/release atomics on `head_`/`tail_`. |
| Integer price ticks (`int64_t`), not `double` | Avoids floating-point comparison/rounding issues on the price axis. |

## Honest performance claims

Only the numbers in your own `latency_results.csv` / `bench` output, run on
your own machine, belong on a resume or in an interview. See
`benchmarks/latency_benchmark.cpp` and `python/analyze_latency.py`.
