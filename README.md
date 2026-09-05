# Low-Latency Limit Order Book & Matching Engine

A C++20 low-latency simulated exchange backend implementing a price-time-priority limit order book, matching engine, TCP gateway, pre-trade risk controls, SPSC queue, market-data replay, latency benchmarking, automated tests, Python analytics, and a FastAPI/WebSocket visualization layer.

> This is a simulated exchange project for software-engineering and performance experimentation. It does not connect to a real exchange or execute real financial orders.

## Architecture

```text
Browser
   |
   | WebSocket / JSON
   v
FastAPI
   |
   | Binary TCP
   v
C++ TCP Gateway
   |
   | SPSC Queue
   v
Matching Engine
   |
   +---- Risk Engine
   |
   +---- Limit Order Book
   |
   v
Trades / ACK / REJECT
   |
   v
FastAPI
   |
   v
Browser
```

The C++ engine is authoritative for order processing and trade generation. The Python web layer maintains a lightweight UI projection of orders and trades received from the C++ engine; it does not independently perform order matching.

## Features

### Matching Engine

- BUY and SELL limit orders
- Price-time priority
- Best bid and best ask
- Order matching
- Partial fills
- Multiple fills for one incoming order
- Order cancellation
- Trade generation
- Remaining quantity tracking
- Price-level cleanup

### Networking

- TCP/IP gateway
- Binary protocol
- Non-blocking sockets
- `kqueue` on macOS
- `epoll` on Linux
- Sequence numbers
- Heartbeats
- ACK responses
- REJECT responses
- TRADE responses
- Outbound response handling

### Concurrency

- SPSC lock-free queue
- Atomic operations
- Network-to-matching-engine producer/consumer path
- Dedicated matching-engine processing

### Risk Management

- Maximum order quantity
- Maximum position
- Maximum notional
- Maximum price deviation
- Daily loss limit
- Duplicate order detection

### Testing and Benchmarking

- GoogleTest order-book and risk tests
- Market-data replay
- Synthetic market-data generation
- Per-order latency measurement
- Throughput measurement
- Python latency analysis
- Latency histogram generation
- Latency-over-time analysis

### Web Visualization

- FastAPI backend
- WebSocket browser communication
- BUY/SELL order entry
- Live order-book display
- Recent-trade display
- C++ gateway integration

## Repository Structure

```text
low-latency-exchange/
├── CMakeLists.txt
├── Dockerfile
├── README.md
├── engine/
│   ├── order.hpp
│   ├── matching_engine.cpp
│   └── order_book.hpp
├── risk/
│   └── risk_engine.hpp
├── concurrency/
│   └── spsc_queue.hpp
├── networking/
│   ├── protocol.hpp
│   └── tcp_server.cpp
├── simulator/
│   └── market_replay.cpp
├── benchmarks/
│   └── latency_benchmark.cpp
├── tests/
│   └── orderbook_test.cpp
├── python/
│   ├── requirements.txt
│   ├── analyze_latency.py
│   └── plot_benchmarks.py
├── web/
│   ├── requirements.txt
│   ├── main.py
│   ├── app.js
│   ├── index.html
│   └── styles.css
└── docs/
    └── architecture.md
```

## Technology Stack

| Component | Technology |
|---|---|
| Language | C++20 |
| Networking | TCP/IP |
| macOS I/O | kqueue |
| Linux I/O | epoll |
| Concurrency | SPSC lock-free queue |
| Testing | GoogleTest |
| API | FastAPI |
| Browser communication | WebSocket |
| Analytics | Python |
| Build | CMake |
| Containerization | Docker |
| Version control | Git |

## Order Model

Orders contain a sequence number, order ID, side, price, and quantity. Supported sides are `BUY` and `SELL`.

Prices are represented internally as integer price ticks. For example, `100.25` can be represented as `10025` with a price scale of 100. Integer prices avoid floating-point comparison issues in the matching path.

## Price-Time Priority

BUY orders have higher priority at higher prices. SELL orders have higher priority at lower prices. At the same price, the earlier order has time priority.

## Matching Example

Existing book:

```text
ASK
100.50 × 100

BID
100.25 × 200
```

Incoming order:

```text
BUY 100.50 × 40
```

Result:

```text
TRADE 100.50 × 40
Remaining ASK: 100.50 × 60
```

## Risk Engine

Available rejection reasons:

```text
NONE
MAX_ORDER_SIZE
MAX_POSITION
MAX_NOTIONAL
PRICE_DEVIATION
DAILY_LOSS_LIMIT
DUPLICATE_ORDER
```

Default configured limits:

```text
Maximum order quantity: 10,000
Maximum position:       100,000
Maximum notional:       50,000,000
Maximum price deviation: 10%
Daily loss limit:       -5,000,000
```

The engine checks projected position, price × quantity notional, configured reference-price deviation, daily loss, and duplicate order IDs before accepting orders.

## Binary TCP Protocol

Messages use:

```text
[message type][payload]
```

Message types:

```text
NEW_ORDER = 1
CANCEL_ORDER = 2
HEARTBEAT = 3
ACK = 10
REJECT = 11
TRADE = 12
```

New-order fields:

```text
uint8   message type
uint64  sequence number
uint64  order ID
uint8   side
int64   price
uint32  quantity
```

Python packing format:

```text
<BQQBqI
```

## Gateway Processing

```text
TCP Client
    |
    v
Non-blocking Socket
    |
    v
kqueue / epoll
    |
    v
Binary Message Decode
    |
    v
SPSC Queue
    |
    v
Risk Validation
    |
    v
Matching Engine
    |
    v
Order Book
    |
    v
Trade / ACK / REJECT
    |
    v
Outbound TCP Response
```

The local C++ gateway listens on port `9000`.

## FastAPI and WebSocket Integration

```text
Browser
   |
   | JSON / WebSocket
   v
FastAPI
   |
   | Binary TCP
   v
C++ Gateway
   |
   v
Matching Engine
```

Trades produced by the C++ engine are sent back through the gateway and FastAPI to the browser.

## Build

```bash
cmake -S . -B build
cmake --build build -j
```

Main executables include:

```text
matching_engine
gateway
bench
replay
orderbook_test
```

## Tests

```bash
cd build
ctest --output-on-failure
```

Verified result: 16 GoogleTest cases pass with 100% test success. Tests cover order-book behavior and risk-engine validation, including maximum notional, price deviation, and daily loss limit checks.

## Matching Engine Benchmark

```bash
./build/matching_engine
```

Verified development-machine run:

```text
Orders:       1,000,000
Time:             62260 us
Throughput:  1.60617e+07 orders/sec
Trades:               3
```

## Latency Benchmark

```bash
./build/bench 1000000 latency_results.csv
```

Verified development-machine result:

```text
Orders:       1,000,000
Trades:         780,801
Total time:      0.171246 s
Throughput:    5.83954e+06 orders/sec

p50:      0.084 us
p90:      0.208 us
p95:      0.250 us
p99:      0.583 us
p99.9:    1.584 us
max:   7959.960 us
```

Environment:

```text
CPU:       Apple M4
OS:        macOS
Compiler:  AppleClang 21.0.0
Language:  C++20
Build:     Release
Flags:     -O3 -DNDEBUG
```

These are development-machine measurements and are not production exchange latency claims.

## Python Latency Analysis

```bash
pip install -r python/requirements.txt
python3 python/analyze_latency.py latency_results.csv
```

The analyzer reports count, mean, p50, p90, p95, p99, p99.9, and maximum latency and generates latency visualizations.

## Market Replay

Generate synthetic data:

```bash
./build/replay --generate 100000 market_data.bin
```

Replay it:

```bash
./build/replay --file market_data.bin --speed 10
```

A verified replay run processed 100,000 events and 77,829 trades.

## Local Web Application

Start the C++ gateway:

```bash
./build/gateway
```

Start FastAPI:

```bash
cd web
python3 main.py
```

Open:

```text
http://localhost:8000
```

Submit crossing BUY and SELL orders to generate trades. The resulting trades are produced by the C++ engine and displayed by the browser through FastAPI/WebSocket.

## Direct TCP Test

```python
import socket
import struct

HOST = "127.0.0.1"
PORT = 9000

sock = socket.create_connection((HOST, PORT))

sequence = 1
order_id = 1
side = 0       # BUY
price = 10025
quantity = 50

message = struct.pack(
    "<BQQBqI",
    1,
    sequence,
    order_id,
    side,
    price,
    quantity
)

sock.sendall(message)
print(sock.recv(1024))
sock.close()
```

For SELL, use `side = 1`.

## Profiling on Linux

```bash
sudo perf stat ./build/matching_engine
sudo perf record ./build/matching_engine
sudo perf report
taskset -c 2 ./build/bench 1000000
```

These tools can be used to identify CPU hotspots and investigate latency variation.

## Reliability

The current design includes handling for duplicate order IDs, invalid requests, risk rejection, partial fills, cancellation requests, client connections, sequence numbers, heartbeats, TCP responses, and network disconnects.

The project does not claim production-grade distributed failover, durable order-book persistence, exchange-grade recovery, or hardware timestamping.

## Docker and Deployment

The repository includes a Dockerfile for containerized deployment. The intended deployment keeps the C++ gateway and FastAPI application together so FastAPI can communicate with the gateway through the container's local network interface.

The public HTTP/WebSocket service should bind to `0.0.0.0` and use the deployment platform's `PORT` environment variable. The C++ gateway remains an internal TCP service.

## Repository Hygiene

Do not commit local/generated files:

```text
build/
.conda/
__pycache__/
*.pyc
*.csv
*.png
market_data.bin
*.log
.DS_Store
```

Recommended `.gitignore`:

```gitignore
build/
cmake-build-*/
__pycache__/
*.py[cod]
.venv/
venv/
.env
.DS_Store
*.csv
*.png
market_data.bin
*.log
.vscode/
.idea/
.pytest_cache/
.mypy_cache/
```

## Future Improvements

Potential future extensions include a custom memory pool, additional cache-conscious data structures, CPU affinity/thread pinning, further lock-free communication, protocol fuzz testing, more complete market-data feed handling, durable event logging, order-book recovery, distributed gateway/matching architecture, and more detailed monitoring.

These are future improvements and are not presented as implemented features.

## Resume Description

### Low-Latency Limit Order Book & Matching Engine

- Built a C++20 price-time-priority limit order book and matching engine supporting partial fills, cancellations, trade generation, and best bid/ask tracking.
- Implemented a non-blocking TCP gateway using kqueue/epoll with binary messaging, sequence numbers, ACK/REJECT/TRADE responses, and an SPSC lock-free queue between networking and matching paths.
- Added pre-trade risk controls for order size, position, notional exposure, price deviation, daily loss, and duplicate orders with GoogleTest coverage.
- Integrated the C++ engine with FastAPI/WebSocket to provide a browser-based simulated exchange visualization and order-entry interface.
- Built market-data replay and latency benchmarking tools; measured 5.84M orders/sec with 0.583 µs p99 latency on an Apple M4 development machine.

## Disclaimer

This repository is an educational and engineering simulation of an electronic trading system. It is not connected to a real exchange, broker, market-data provider, or financial account. Benchmark results are machine-dependent and should not be interpreted as production trading-system performance. No real financial orders are executed by this project.
