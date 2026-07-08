#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "binance_parser.h"
#include "config.h"
#include "csv_writer.h"
#include "metrics.h"
#include "order_book.h"

namespace blc {

// Deliverable B: consumes parsed events,maintains one OrderBook per symbol,
// runs the venue-specific sequence checks, and emits one orderbook CSV row
// per applied depthUpdate / depth5. 

// This class is shared verbatim between live mode and replay mode, which is
// what makes "replay reproduces the order book CSV" true by construction.
class BookPipeline {
public:
    // `symbols` fixes the instrument-id mapping: id = index in this list.
    BookPipeline(std::string output_dir, Venue venue,
                 const std::vector<std::string>& symbols, Metrics& metrics);

    // Called on every parsed event, in processing order.
    void process(const Event& ev);

    // policy is to clear every book and re-converge from fresh diffs.
    void on_reconnect();

    void flush_all();

    // Exact header from the assignment — must match byte-for-byte.
    static constexpr const char* kHeader =
        "tsec,tnsec,seqNo,id,type,side,"
        "bid0,bid1,bid2,bid3,bid4,"
        "bid_size0,bid_size1,bid_size2,bid_size3,bid_size4,"
        "ask0,ask1,ask2,ask3,ask4,"
        "ask_size0,ask_size1,ask_size2,ask_size3,ask_size4";

private:
    struct SymbolState {
        int32_t id = 0;                 // index in configured symbol list
        OrderBook book;
        uint64_t seq_no = 0;            // one increment per emitted row
        int64_t last_final_id = -1;     // last applied u (-1 = none yet)
        std::unique_ptr<CsvFile> file;  // opened lazily on first event
    };

    SymbolState* state_for(const std::string& symbol, int64_t tsec);

    // Returns true if the diff continues the sequence, false on a gap.
    bool sequence_ok(const SymbolState& st, const DepthDiffData& d) const;

    void emit_row(SymbolState& st, const Event& ev, char type, char side,
                  const TopLevels& bids, const TopLevels& asks);

    std::string output_dir_;
    Venue venue_;
    Metrics& metrics_;
    std::unordered_map<std::string, SymbolState> states_;
    std::unordered_map<std::string, int32_t> id_by_symbol_;
    int32_t next_unknown_id_;  // ids for symbols not in the configured list
    std::string row_;          // reused row buffer
};

}  // namespace blc
