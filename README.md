# Binance WebSocket Capture + Local Order Book

Captures Binance combined WebSocket streams (`depth@100ms`, `depth5@100ms`,
`trade`) for configured symbols, writes a **market-data CSV** (one row per
inbound event) and a **local order book snapshot CSV** (one row per applied
book event), and can **replay** the order book CSV from a saved market-data
CSV with zero network calls.

## Toolchain

- **Language / standard:** C++17
- **Compiler:** GCC 12+ (tested with GCC 13.3). Clang 15+ also works.
- **Build:** CMake >= 3.16, `-Wall -Wextra`, Release build. No warnings.
- **Dependencies:**
  - Boost (headers only: Beast + Asio) — WebSocket client
  - OpenSSL — TLS (certificate verification is **enabled**)
  - simdjson v4.6.4 — vendored in `third_party/` (amalgamated release),
    chosen as the "efficient parser" option; one long-lived
    `dom::parser` reuses its internal buffers so steady-state parsing does
    not heap-allocate per message.

```bash
# Ubuntu 22.04/24.04
sudo apt-get install -y cmake g++ libboost-dev libssl-dev

cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

## CLI

```bash
# Live capture (both CSVs), single symbol, 5 minutes:
./build/binance_capture --venue spot --symbols BTCUSDT \
    --output-dir ./output --duration 300

# Multi-symbol (comma separated, max 10 per run):
./build/binance_capture --venue spot --symbols BTCUSDT,ETHUSDT --output-dir ./output

# USD-M futures:
./build/binance_capture --venue usdm --symbols BTCUSDT --output-dir ./output

# Replay (NO network): regenerate the orderbook CSV from a market-data CSV.
# Venue is auto-detected from the file. Pass the same --symbols list as the
# live run to reproduce identical instrument ids.
./build/binance_capture --replay ./output/market_data_spot_BTCUSDT_2026-07-06.csv \
    --symbols BTCUSDT --output-dir ./replay_out

# Help:
./build/binance_capture --help

# Tests:
./build/unit_tests
```

`--duration 0` (default) runs until SIGINT/SIGTERM. Ctrl-C flushes and
closes all files cleanly.

**Symbol list format:** comma-separated, case-insensitive (`btcusdt` and
`BTCUSDT` both accepted; stored uppercase in CSVs, lowercased in the URL).

## Output files

One file pair per `(venue, symbol)`, named with the UTC date of the first
event seen for that symbol:

```
output/
  market_data_spot_BTCUSDT_2026-07-06.csv            (Deliverable A)
  market_data_spot_BTCUSDT_2026-07-06_orderbook.csv  (Deliverable B)
```

Documented choice: **one file per symbol** (per the sample layout in the
task), rather than one multi-symbol file. A run that crosses UTC midnight
keeps writing to the file it opened (the date names the session start).

## Documented policies

### Time policy (recommended option)
`recv_tsec` / `recv_tnsec` are the **wall clock (CLOCK_REALTIME) at the
moment the process finished reading the message**, split into whole seconds
and nanosecond remainder in `[0, 999999999]`.

The orderbook CSV's `tsec`/`tnsec` for an event **reuse the exact
`recv_tsec`/`recv_tnsec` of the market-data row for that same event**. This
is deliberate: it is what makes replay reproduce the orderbook CSV
**byte-identically** (verified with `diff` in CI-style tests).

### Integer scaling (prices and quantities)
Fixed scale **10^8** for both prices and quantities, applied by pure string
arithmetic (`src/utils.cpp: scale_decimal`). Binance decimal strings are
parsed digit-by-digit: integer part × 10^8 + fraction padded/truncated to
exactly 8 digits. **No floating point exists anywhere in the path from
socket to CSV integer**, so results are deterministic. Example:
`"25000.12345678"` -> `2500012345678`. Fraction digits beyond the 8th are
truncated (Binance uses <= 8 on these instruments).

### stream_kind encoding
Strings: `depth_diff` | `depth5` | `trade`. Classification comes from the
combined-stream **envelope's `stream` name**, never from the payload
(the spot `depth5` payload has no `"e"` field, and the futures `depth5`
payload is shaped like a `depthUpdate` — the stream name is the only
reliable discriminator).

### Order book semantics
- Book = two `std::map<int64 price, int64 qty>` (bids, asks). Applying a
  `depthUpdate` is O(levels x log N); reading the top 5 is O(5).
- `depthUpdate` quantities are **absolute replacements**; qty `0` removes
  the level; removing an unknown level is a legal no-op.
- **`depth5` is never merged into the diff book.** It has replace semantics
  for the top 5 only; merging a 5-level truncation into a full-depth book
  would corrupt it (a level absent from depth5 may simply be below rank 5,
  not gone). depth5 rows are emitted directly from the depth5 payload.
- **Trades never modify the book** (a diff-based book already reflects
  their effect via subsequent depthUpdates). Trades appear only in the
  market-data CSV; no orderbook row is emitted for them.

### Orderbook CSV column mapping
- `type`: `D` = depthUpdate applied, `P` = depth5 partial snapshot.
- `side`: `B` if the diff touched only bids, `S` only asks, `N` both /
  symmetric (all `P` rows are `N`).
- `id`: index of the symbol in the `--symbols` list (0-based), stable for
  the session. In replay, pass the same `--symbols` to reproduce the same
  ids (otherwise ids are assigned by first appearance, documented fallback).
- `seqNo`: per-symbol counter, +1 per emitted row, starts at 0.
- Fewer than 5 levels on a side -> remaining price/size columns are `0`.

### Reconnects, sequence numbers, and gaps
- `shard_id` = 0 (single connection per run).
- `conn_epoch` starts at 0 and **increments on every reconnect**;
  `conn_seq` restarts at 0 per epoch and is monotonic within
  `(shard_id, conn_epoch)`.
- Gap detection on the diff stream, per venue:
  - **spot:** expect `U == previous u + 1`;
  - **usdm:** expect `pu == previous u`.
- **On a detected gap or a reconnect, the book is cleared** and re-converges
  from subsequent diffs (counted in metrics as `sequence_gaps` /
  `reconnects`). The base scope starts mid-stream without a REST snapshot,
  so the book converges over time as levels churn; REST snapshot + diff
  buffer resync is the documented stretch goal, not implemented here.
  This policy is intentionally simple, loss-aware, and consistent between
  live and replay (the same gap logic runs in both).

### Design note: threading and I/O
**Single-threaded by design.** One thread performs: read message -> stamp
receive time -> parse once (simdjson) -> write market-data row -> apply to
book -> write orderbook row. Rationale:

- The assignment requires rows to reflect **processing order**; a single
  thread makes that ordering trivially correct with zero synchronization,
  no data races, and no contended locks.
- Measured load is tiny relative to capacity: ~30–60 msg/s per symbol
  (depth + depth5 at 100 ms cadence + trades); the pipeline handles tens of
  thousands of msg/s in replay on one core, so a writer thread would add
  complexity without need.
- I/O back-pressure: both CSV files use a 1 MiB stream buffer, so the hot
  path almost always memcpy's into the buffer instead of hitting a syscall.
  If the disk stalls, the TCP receive window applies natural back-pressure.

Hot-path allocation behaviour: one long-lived simdjson parser (internal
buffer reuse), reused `std::string` row buffers, `reserve()` on level
vectors. Steady-state per-message heap usage is limited to the payload
string and level vectors of the `Event`.

Durability: in addition to RAII flushing on every clean exit, live mode
flushes both CSV buffers every 5 seconds, so even an unclean kill
(SIGKILL, power loss) loses at most ~5 s of rows instead of a full write
buffer.

Shutdown: SIGINT/SIGTERM set a `volatile sig_atomic_t` flag checked between
messages; worst-case shutdown latency is the inter-message gap (~100 ms on
an active symbol). All files are RAII-owned and flushed on every exit path;
clean under AddressSanitizer/UBSan.

### Metrics
Printed to stderr at exit:
`messages, parse_errors, reconnects, sequence_gaps, market_data_rows, orderbook_rows`.

### Security / ops
- No credentials anywhere (public streams only); nothing secret in the repo.
- TLS certificate verification enabled (`ssl::verify_peer` + system CA
  bundle); SNI set explicitly.
- Max 10 symbols per run caps URL length and memory.

### URL shapes (from the task sheet)
- spot: `wss://stream.binance.com:9443/stream?streams=...`
- usdm: `wss://fstream.binance.com/public/stream?streams=...`
  (the `/public` path is used exactly as specified in the task sheet)

## Sample run

`samples/` contains a small deterministic sample produced by the simulated
tape in `tests/sim_live.cpp` (same code path as live capture), plus the
replay verification:

```bash
./build/sim_live ./samples            # produces both CSVs from a fixed tape
./build/binance_capture --replay samples/market_data_spot_*.csv \
    --symbols BTCUSDT --output-dir ./samples_replay
diff samples/*_orderbook.csv samples_replay/*_orderbook.csv   # identical
```

For grading, run a real 1–2 minute live capture with the CLI above and
replay it the same way; the regenerated `*_orderbook.csv` is byte-identical
because orderbook timestamps derive from the stored receive timestamps.

**No network calls occur in replay mode** — the binary never opens a socket
when `--replay` is given.

## Verify CSV schema

```bash
head -2 output/market_data_spot_BTCUSDT_*.csv
head -2 output/market_data_spot_BTCUSDT_*_orderbook.csv
awk -F',' 'NR==2{print NF}' output/*_orderbook.csv    # expect 26
```

## Project layout

```
src/
  main.cpp              wiring: CLI, signals, live vs replay
  config.*              CLI parsing -> Config
  utils.*               time split, decimal->scaled int64 (no floats)
  csv_writer.*          RFC 4180 escape/parse + buffered RAII file
  binance_parser.*      envelope split, stream classification, typed events
  order_book.*          std::map book, absolute-diff semantics, top-5
  market_data_writer.*  Deliverable A rows
  book_pipeline.*       gap checks, book state, Deliverable B rows
  websocket_client.*    Beast TLS client, reconnect + conn_epoch
  replay_engine.*       market-data CSV -> same BookPipeline, no network
tests/
  unit_tests.cpp        offline module tests (assert-based, no deps)
  sim_live.cpp          deterministic live-path simulation tape
third_party/simdjson.*  vendored parser
```
