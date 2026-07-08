#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

#include "binance_parser.h"
#include "config.h"
#include "csv_writer.h"

namespace blc {

// Deliverable A: one CSV row per inbound message, in processing order.
// One file per (venue, symbol, UTC date), matching the sample layout in the
class MarketDataWriter {
public:
    MarketDataWriter(std::string output_dir, Venue venue);

    // conn_* identifiers come from the connection layer, not the event.
    void write(const Event& ev, uint32_t shard_id, uint32_t conn_epoch,
               uint64_t conn_seq);

    void flush_all();
    uint64_t total_rows() const { return total_rows_; }

    static constexpr const char* kHeader =
        "recv_tsec,recv_tnsec,venue,stream_kind,shard_id,conn_epoch,"
        "conn_seq,symbol,payload_json";

private:
    CsvFile& file_for(const std::string& symbol, int64_t tsec);

    std::string output_dir_;
    Venue venue_;
    std::unordered_map<std::string, std::unique_ptr<CsvFile>> files_;
    std::string row_;  
    uint64_t total_rows_ = 0;
};

}  
