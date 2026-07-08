#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "config.h"
#include "order_book.h"
#include "utils.h"

namespace blc {

enum class StreamKind { DepthDiff, Depth5, Trade };

const char* stream_kind_name(StreamKind k);  // "depth_diff"/"depth5"/"trade"
std::optional<StreamKind> stream_kind_from_string(std::string_view s);

// Typed content of a depthUpdate event, prices/qtys already scaled.
struct DepthDiffData {
    int64_t first_update_id = 0;              // U
    int64_t final_update_id = 0;              // u
    std::optional<int64_t> prev_final_id;     // pu (USD-M futures only)
    std::vector<LevelUpdate> bids;
    std::vector<LevelUpdate> asks;
};

// Typed content of a depth5 partial snapshot (top-5 replace semantics).
struct Depth5Data {
    std::vector<LevelUpdate> bids;  // best-first, at most 5
    std::vector<LevelUpdate> asks;
};

// One logical event flowing through the pipeline. `payload` is the inner

struct Event {
    SplitTime recv;
    StreamKind kind = StreamKind::Trade;
    std::string symbol;   // uppercase
    std::string payload;  // minified inner JSON

    // Exactly one of these is set, depending on `kind` (trades carry no
    // typed data because they never touch the book).
    std::optional<DepthDiffData> depth_diff;
    std::optional<Depth5Data> depth5;
};

// Wraps a simdjson parser so its internal buffers are reused across
// "avoid per-message parse allocations" story).
class BinanceParser {
public:
    BinanceParser();
    ~BinanceParser();

    BinanceParser(const BinanceParser&) = delete;
    BinanceParser& operator=(const BinanceParser&) = delete;

    // Live path: raw combined-stream message ("{"stream":...,"data":{...}}").
  
    // payload AND the typed data in a single parse.
    std::optional<Event> parse_live_message(std::string_view msg,
                                            SplitTime recv);

    // Replay path: we already know kind/symbol from the CSV columns and
    // only need to re-parse the stored payload into typed data.
    bool parse_payload_into(Event& event);

private:
    struct Impl;                 // hides simdjson from every other header
    std::unique_ptr<Impl> impl_;
};

}  // namespace blc
