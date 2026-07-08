# Binance WebSocket Capture + Local Order Book

A low-latency C++17 market data capture tool and local order book (LOB) engine for Binance Spot and USD-M Futures. 

It connects to Binance's combined WebSocket streams (`depth@100ms`, `depth5@100ms`, and `trade`), records raw market data events to an RFC 4180 CSV file, and maintains a real-time local order book snapshot (top 5 bids and asks) after every update. It also includes an offline replay engine and a standalone HTML visualizer.

---

## Key Features

- **Live Multi-Stream Capture:** Streams and parses `depth@100ms`, `depth5@100ms`, and `trade` events over TLS.
- **Deterministic Order Book:** Maintains top-5 bids and asks using exact integer arithmetic (fixed $10^8$ scaling — zero floating point representation drift).
- **Offline Byte-Identical Replay:** Reconstructs the exact orderbook CSV from recorded raw market data without making network requests.
- **Single-Threaded Pipeline:** Eliminates locks, race conditions, and out-of-order processing by design.
- **Interactive Web Demo:** Includes a standalone HTML/JS order book visualizer with playback controls and CSV drag-and-drop support.

---

## Getting Started

### Prerequisites
- **C++17 Compiler:** GCC 12+ or Clang 15+ (tested on Ubuntu Linux and macOS AppleClang 21)
- **CMake:** 3.16 or newer
- **Dependencies:** Boost (headers only), OpenSSL

### Build Instructions

```bash
# Ubuntu Linux
sudo apt-get install -y cmake g++ libboost-dev libssl-dev

# macOS (Homebrew)
brew install cmake boost openssl

# Build the Release binary
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

---

## Usage

### 1. Live Capture

Stream live Binance Spot data for `BTCUSDT` and save outputs to `./output` for 60 seconds:
```bash
./build/binance_capture --venue spot --symbols BTCUSDT --output-dir ./output --duration 60
```
*(Omit `--duration` or pass `--duration 0` to stream continuously until you press `Ctrl+C`).*

Capture multiple symbols simultaneously (up to 10):
```bash
./build/binance_capture --venue spot --symbols BTCUSDT,ETHUSDT,SOLUSDT --output-dir ./output
```

Capture USD-M Futures:
```bash
./build/binance_capture --venue usdm --symbols BTCUSDT --output-dir ./output
```

### 2. Output Files

For each symbol, two files are created per UTC day:
1. **Raw Market Data (`Deliverable A`):**  
   `output/market_data_spot_BTCUSDT_YYYY-MM-DD.csv`  
   Contains raw stream payloads, shard IDs, sequence numbers, and receive timestamps.
2. **Order Book Snapshots (`Deliverable B`):**  
   `output/market_data_spot_BTCUSDT_YYYY-MM-DD_orderbook.csv`  
   26-column schema containing top 5 bids/asks and quantities updated after every diff or snapshot.

---

### 3. Offline Replay Mode

You can regenerate the orderbook CSV from a recorded market data file without internet access:

```bash
./build/binance_capture \
  --replay ./output/market_data_spot_BTCUSDT_2026-07-08.csv \
  --symbols BTCUSDT \
  --output-dir ./replay_output
```

Verify that the offline replay is **byte-identical** to the live run:
```bash
diff -s output/market_data_spot_BTCUSDT_2026-07-08_orderbook.csv \
        replay_output/market_data_spot_BTCUSDT_2026-07-08_orderbook.csv
```

---

## Interactive Web Visualizer

The repository includes a standalone web app inside `samples/lob_replay_visualizer.html` to visually demonstrate the order book in action.

- **Open Locally:** Double-click `samples/lob_replay_visualizer.html` or run `open samples/lob_replay_visualizer.html` in your terminal.
- **View Live Online:** [GitHub Pages Interactive Demo](https://iokraken.github.io/Irage_Assesment/samples/lob_replay_visualizer.html)
- **Features:**
  - Drag and drop any `*_orderbook.csv` file produced by the C++ app to replay the session.
  - Built-in synthetic demo tape generator (`Synthetic demo tape` button).
  - Live ladder chart, spread monitor (in bps), and interactive event tape.

---

## Architecture & Engineering Decisions

### Why Single-Threaded?
Market data ordering matters. Processing socket reads, timestamping, JSON parsing, and order book updates on a single thread ensures strict processing order with zero lock contention or synchronization overhead. With `simdjson` buffer reuse, the pipeline easily handles tens of thousands of messages per second — well above Binance's ~30–60 msg/s per symbol.

### Exact Integer Scaling ($10^8$)
All prices and quantities are parsed from JSON strings directly into 64-bit integers scaled by $10^8$ (`src/utils.cpp: scale_decimal`). For example, `"67005.12"` becomes `6700512000000`. This avoids IEEE 754 floating-point inaccuracies across machines.

### Sequence Validation & Gap Handling
- **Spot:** Validates `U == previous_u + 1`
- **Futures:** Validates `pu == previous_u`
- If a sequence gap or WebSocket reconnect occurs, the order book state is cleared and re-converges cleanly from subsequent updates.

---

## Running Tests

```bash
# Run automated unit tests (decimal scaling, CSV escaping, book semantics)
./build/unit_tests

# Run simulated live capture against test tape
./build/sim_live ./test_output
```

---

## Repository Structure

```
src/
  main.cpp              CLI entrypoint, signal handlers, live & replay orchestration
  websocket_client.*    Boost Beast TLS WebSocket connection & reconnect logic
  binance_parser.*      simdjson wrapper and stream classification
  order_book.*          Top-5 level order book implementation
  book_pipeline.*       Sequence validation, gap handling, and orderbook CSV emission
  csv_writer.*          RFC 4180 buffered file writer (1 MiB stream buffer)
  utils.*               Integer scaling arithmetic and timestamp formatting
tests/
  unit_tests.cpp        Automated unit tests
  sim_live.cpp          Deterministic simulation tape test
samples/
  lob_replay_visualizer.html   Interactive browser-based LOB visualizer
  market_data_*.csv            Sample live-captured market data & orderbook CSVs
```
