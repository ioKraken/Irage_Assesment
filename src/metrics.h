#pragma once

#include <cstdint>
#include <cstdio>

namespace blc {

// Plain counters — single-threaded pipeline, so no atomics needed.
struct Metrics {
    uint64_t messages_received = 0;
    uint64_t parse_errors = 0;
    uint64_t reconnects = 0;
    uint64_t sequence_gaps = 0;
    uint64_t market_data_rows = 0;
    uint64_t orderbook_rows = 0;

    void print(std::FILE* out) const {
        std::fprintf(out,
                     "[metrics] messages=%llu parse_errors=%llu reconnects=%llu "
                     "sequence_gaps=%llu market_data_rows=%llu orderbook_rows=%llu\n",
                     (unsigned long long)messages_received,
                     (unsigned long long)parse_errors,
                     (unsigned long long)reconnects,
                     (unsigned long long)sequence_gaps,
                     (unsigned long long)market_data_rows,
                     (unsigned long long)orderbook_rows);
    }
};

}  // namespace blc
