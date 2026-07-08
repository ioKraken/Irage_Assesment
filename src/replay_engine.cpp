#include "replay_engine.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>

#include "csv_writer.h"
#include "market_data_writer.h"

namespace blc {

std::optional<Venue> ReplayEngine::detect_venue(const std::string& path) {
    std::ifstream in(path, std::ios::in | std::ios::binary);
    if (!in.is_open()) return std::nullopt;
    std::string line;
    if (!std::getline(in, line)) return std::nullopt;  // header
    if (!std::getline(in, line)) return std::nullopt;  // first data row
    std::vector<std::string> fields = parse_csv_line(line);
    if (fields.size() < 3) return std::nullopt;
    return venue_from_string(fields[2]);  // venue column
}

ReplayEngine::ReplayEngine(BinanceParser& parser, BookPipeline& pipeline,
                           Metrics& metrics)
    : parser_(parser), pipeline_(pipeline), metrics_(metrics) {}

bool ReplayEngine::run(const std::string& path) {
    std::ifstream in(path, std::ios::in | std::ios::binary);
    if (!in.is_open()) {
        std::fprintf(stderr, "[replay] cannot open %s\n", path.c_str());
        return false;
    }

    std::string line;
    if (!std::getline(in, line)) {
        std::fprintf(stderr, "[replay] empty file\n");
        return false;
    }
    
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line != MarketDataWriter::kHeader) {
        std::fprintf(stderr, "[replay] unexpected header, not a market data CSV\n");
        return false;
    }

    // Column indexes in the market-data CSV (fixed by the header contract).
    constexpr size_t kRecvTsec = 0, kRecvTnsec = 1, kStreamKind = 3,
                     kSymbol = 7, kPayload = 8, kNumCols = 9;

    uint64_t line_no = 1;
    while (std::getline(in, line)) {
        ++line_no;
        if (line.empty()) continue;
        ++metrics_.messages_received;

        std::vector<std::string> fields = parse_csv_line(line);
        if (fields.size() != kNumCols) {
            ++metrics_.parse_errors;
            std::fprintf(stderr, "[replay] line %llu: bad column count\n",
                         (unsigned long long)line_no);
            continue;
        }

        auto kind = stream_kind_from_string(fields[kStreamKind]);
        if (!kind) {
            ++metrics_.parse_errors;
            continue;
        }

        // strtoll returns 0 on garbage without complaining; check that the
        // whole field was consumed so corrupted rows are counted, not
        // silently replayed with timestamp 0.
        auto parse_int = [](const std::string& s, int64_t& out) -> bool {
            if (s.empty()) return false;
            char* end = nullptr;
            out = std::strtoll(s.c_str(), &end, 10);
            return end == s.c_str() + s.size();
        };

        Event ev;
        int64_t tnsec64 = 0;
        if (!parse_int(fields[kRecvTsec], ev.recv.tsec) ||
            !parse_int(fields[kRecvTnsec], tnsec64) ||
            tnsec64 < 0 || tnsec64 > 999'999'999) {
            ++metrics_.parse_errors;
            std::fprintf(stderr, "[replay] line %llu: bad timestamp\n",
                         (unsigned long long)line_no);
            continue;
        }
        ev.recv.tnsec = static_cast<int32_t>(tnsec64);
        ev.kind = *kind;
        ev.symbol = fields[kSymbol];
        ev.payload = std::move(fields[kPayload]);

        if (!parser_.parse_payload_into(ev)) {
            ++metrics_.parse_errors;
            continue;
        }
        pipeline_.process(ev);
    }
    pipeline_.flush_all();
    return true;
}

}  // namespace blc
