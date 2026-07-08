#include "market_data_writer.h"

#include "utils.h"

namespace blc {

MarketDataWriter::MarketDataWriter(std::string output_dir, Venue venue)
    : output_dir_(std::move(output_dir)), venue_(venue) {
    row_.reserve(1024);
}

CsvFile& MarketDataWriter::file_for(const std::string& symbol, int64_t tsec) {
    auto it = files_.find(symbol);
    if (it == files_.end()) {
        std::string path = output_dir_ + "/market_data_" +
                           venue_name(venue_) + "_" + symbol + "_" +
                           utc_date_string(tsec) + ".csv";
        it = files_.emplace(symbol,
                            std::make_unique<CsvFile>(path, kHeader)).first;
    }
    return *it->second;
}

void MarketDataWriter::write(const Event& ev, uint32_t shard_id,
                             uint32_t conn_epoch, uint64_t conn_seq) {
    row_.clear();
    row_.append(std::to_string(ev.recv.tsec));
    row_.push_back(',');
    row_.append(std::to_string(ev.recv.tnsec));
    row_.push_back(',');
    row_.append(venue_name(venue_));
    row_.push_back(',');
    row_.append(stream_kind_name(ev.kind));
    row_.push_back(',');
    row_.append(std::to_string(shard_id));
    row_.push_back(',');
    row_.append(std::to_string(conn_epoch));
    row_.push_back(',');
    row_.append(std::to_string(conn_seq));
    row_.push_back(',');
    row_.append(ev.symbol);
    row_.push_back(',');
    append_csv_field(row_, ev.payload);  // the only field that may need quoting

    file_for(ev.symbol, ev.recv.tsec).write_row(row_);
    ++total_rows_;
}

void MarketDataWriter::flush_all() {
    for (auto& [sym, file] : files_) file->flush();
}

}  // namespace blc
