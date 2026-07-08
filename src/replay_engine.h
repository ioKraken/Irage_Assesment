#pragma once

#include <string>

#include "binance_parser.h"
#include "book_pipeline.h"
#include "metrics.h"

namespace blc {

// Reads a saved market_data_*.csv and pushes every row through the SAME
// BookPipeline used in live mode — zero network calls. Because orderbook

class ReplayEngine {
public:
    ReplayEngine(BinanceParser& parser, BookPipeline& pipeline,
                 Metrics& metrics);

    // Returns false on a fatal problem (file missing / bad header).
    bool run(const std::string& market_data_csv_path);

    // Peeks at the first data row to recover the venue the file was
   
    static std::optional<Venue> detect_venue(const std::string& path);

private:
    BinanceParser& parser_;
    BookPipeline& pipeline_;
    Metrics& metrics_;
};

}  // namespace blc
