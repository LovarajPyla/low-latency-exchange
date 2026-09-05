# Low-Latency Limit Order Book & Matching Engine

A price-time-priority limit order book and matching engine in C++20, with a
TCP gateway, pre-trade risk engine, market-data replay, latency/throughput
benchmarking, automated tests, Python analytics, and a small FastAPI web
demo. Built to the plan in `Low_Latency_Limit_Order_Book_HFT_Project_Guide.pdf`.

**Honesty note (read this before touching your resume):** the C++ code in
`engine/`, `risk/`, `concurrency/`, `networking/`, `simulator/` and
`benchmarks/` is the real engineering artifact. The web app in `web/` is a
separate, much simpler Python re-implementation whose only job is to give a
recruiter something to click in a browser — see `docs/architecture.md`. Only
put a resume bullet or a benchmark number here once you've built, run, and
measured it yourself on your own machine.

## Project layout

```
engine/          Order book + matching engine (the core data structure)
risk/            Pre-trade risk checks (qty/position/notional/price/PnL)
concurrency/     Lock-free SPSC ring buffer
networking/      TCP gateway: epoll, non-blocking sockets, binary protocol
simulator/       Market-data replay (generate + replay synthetic workloads)
benchmarks/      Latency/throughput benchmark, writes CSV
tests/           GoogleTest unit tests
python/          Latency/benchmark analysis + plotting scripts
web/             FastAPI + WebSocket browser demo (Python-only, see above)
docs/            Architecture notes
CMakeLists.txt   Builds everything except the web app
Dockerfile       Multi-stage build: compiles C++, ships the web demo
```

## 1. Build and run the C++ engine locally

Requires a C++20 compiler (g++ 10+/clang 12+) and, optionally, CMake 3.16+.

### Quickest path: single-file MVP (matches the guide's Section 6/7)

```bash
g++ -std=c++20 -O3 -DNDEBUG engine/matching_engine.cpp -I. -o matching_engine
./matching_engine
```

This prints the order book before/after a few sample orders, a cancellation,
and a 1,000,000-order throughput benchmark.

### Full build via CMake (engine, gateway, replay, benchmark, tests)

```bash
mkdir build && cd build
cmake ..
cmake --build . -j
ctest --output-on-failure   # runs the 15 GoogleTest cases, if gtest is installed
```

If GoogleTest isn't installed, the test target is skipped automatically; install
it with `apt install libgtest-dev cmake` (Debian/Ubuntu) to enable it.

This produces four binaries in `build/`:

| Binary | What it does |
|---|---|
| `matching_engine` | Standalone demo + benchmark (no networking) |
| `gateway` | TCP order-entry gateway on port 9000 (epoll + SPSC queue + risk + matching thread) |
| `replay` | Generates/replays synthetic market-data workloads |
| `bench` | Latency benchmark; writes `latency_results.csv` |

For your own machine's numbers (not for a Docker/cloud build host), rebuild
with `-march=native`:

```bash
cmake .. -DCMAKE_CXX_FLAGS_RELEASE="-O3 -march=native -DNDEBUG"
cmake --build . -j
```

### Try the TCP gateway

```bash
./build/gateway &
```

It listens on `127.0.0.1:9000` using the fixed-width binary protocol defined
in `networking/protocol.hpp` (`[uint8 type][payload]`, no delimiter
scanning). A `NewOrderMsg` is:
`uint8 type=1, uint64 seq, uint64 order_id, uint8 side(0=BUY/1=SELL), int64 price, uint32 qty`,
packed with `#pragma pack(push,1)`. You can drive it from any language that
can pack that struct over a socket — for a quick manual test in Python:

```python
import socket, struct
s = socket.create_connection(("127.0.0.1", 9000))
def new_order(seq, oid, side, price, qty):
    return struct.pack("<BQQBqI", 1, seq, oid, side, price, qty)
s.sendall(new_order(1, 1, 1, 10050, 100))  # SELL 100 @ 10050
s.sendall(new_order(2, 2, 0, 10050, 50))   # BUY  50  @ 10050 -> crosses, trades
```

The gateway process prints `TRADE buy=... sell=... price=... qty=...` to
stdout when orders cross.

### Replay and benchmark

```bash
./build/replay --generate 1000000 market_data.bin   # reproducible synthetic workload
./build/replay --file market_data.bin --speed 10     # replay at 10x wall-clock speed

./build/bench 1000000 latency_results.csv
pip install -r python/requirements.txt --break-system-packages
python3 python/analyze_latency.py latency_results.csv
```

`analyze_latency.py` prints p50/p90/p95/p99/p99.9/max and writes two PNGs
(a histogram and a latency-over-time plot) next to the CSV.

### Profiling

```bash
sudo perf stat ./build/matching_engine
sudo perf record ./build/matching_engine && sudo perf report
taskset -c 2 ./build/bench 1000000
```

## 2. Run the web demo locally

```bash
cd web
pip install -r requirements.txt --break-system-packages   # or use a venv
uvicorn main:app --reload --port 8000
```

Open `http://localhost:8000`. Place a BUY and a crossing SELL and you'll see
the book and trade feed update live over the WebSocket.

## 3. Deploy the web demo online

The `Dockerfile` at the repo root builds the C++ binaries in one stage and
copies them, plus the FastAPI app, into a slim Python 3.12 runtime image. The
container serves the web demo on `$PORT` (defaults to 8000) and also ships
`bin/matching_engine`, `bin/gateway`, `bin/replay`, `bin/bench` so you can
`docker exec` in and run the real engine/benchmarks inside the deployed
container if you want to show that off too.

### Build and run the container locally first

```bash
docker build -t low-latency-exchange .
docker run -p 8000:8000 low-latency-exchange
# open http://localhost:8000
```

### Option A — Render (free tier, simplest)

1. Push this repo to GitHub.
2. In Render: **New → Web Service**, connect the repo.
3. Environment: **Docker**. Render auto-detects the `Dockerfile`.
4. Leave the port as default — Render sets `$PORT` and the container's
   `CMD` already reads it.
5. Deploy. Render gives you a public `https://<service>.onrender.com` URL.

### Option B — Fly.io

```bash
brew install flyctl   # or the Linux install script from fly.io/docs
fly launch            # detects the Dockerfile, creates fly.toml
fly deploy
fly open
```

### Option C — Railway

1. Push to GitHub.
2. In Railway: **New Project → Deploy from GitHub repo**.
3. Railway detects the `Dockerfile` automatically and builds/deploys it.
4. Under **Settings → Networking**, generate a public domain.

### Deployment caution

This is a simulated/demo exchange for a portfolio project. It uses simulated
orders and simulated market data only — never connect it to a real brokerage
account or represent it as a production trading system.

## 4. Resume bullets — only claim what you've actually run

See `Low_Latency_Limit_Order_Book_HFT_Project_Guide.pdf`, Section 20, for the
baseline-MVP wording and the list of features to add before claiming them
(TCP gateway ✅ now implemented, SPSC queue ✅ now implemented, risk engine ✅
now implemented — but you still need to run the benchmark and tests
yourself, on your own machine, before writing any specific number down).

## 5. Suggested next steps (not yet implemented)

- Bridge the web UI to the real C++ gateway (WebSocket <-> TCP translation)
  instead of the Python demo book — see `docs/architecture.md`.
- Custom hash table / object pool to replace `std::unordered_map` and heap
  allocation on the hot path, then re-run `bench` to measure the delta.
- Fuzz the TCP gateway's message framing with libFuzzer.
- CPU-affinity pin the network thread and matching thread to separate cores
  (`taskset`) and measure whether that changes tail latency.
