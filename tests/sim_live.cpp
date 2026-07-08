// Simulates the live path without a network: feeds raw combined-stream
// messages through parser -> MarketDataWriter -> BookPipeline, exactly like
// main.cpp's on_message. Used to test replay determinism and gap handling.
#include <cstdio>
#include <string>
#include <vector>

#include "binance_parser.h"
#include "book_pipeline.h"
#include "market_data_writer.h"
#include "metrics.h"

using namespace blc;

int main(int argc, char** argv) {
    std::string out_dir = argc > 1 ? argv[1] : "./sim_output";
    Venue venue = (argc > 2 && std::string(argv[2]) == "usdm") ? Venue::Usdm
                                                               : Venue::Spot;

    Metrics metrics;
    BinanceParser parser;
    std::vector<std::string> symbols = {"BTCUSDT"};
    MarketDataWriter md(out_dir, venue);
    BookPipeline pipeline(out_dir, venue, symbols, metrics);

    // A realistic message tape: depth5 snapshot, contiguous diffs, a trade,
    // a level removal, a SEQUENCE GAP (U jumps), and post-gap recovery.
    struct Msg { int64_t tsec; int32_t tnsec; const char* raw; };

    // Futures tape: depthUpdate carries pu; second->third has a pu GAP
    // (pu=210 while previous u=205), which must clear the book.
    std::vector<Msg> futures_tape = {
        {1750000100, 100000000,
         R"({"stream":"btcusdt@depth@100ms","data":{"e":"depthUpdate","E":1,"T":1,"s":"BTCUSDT","U":200,"u":205,"pu":190,"b":[["60000.00","2.0"],["59999.00","1.0"]],"a":[["60001.00","3.0"]]}})"},
        {1750000100, 200000000,
         R"({"stream":"btcusdt@depth5@100ms","data":{"e":"depthUpdate","E":2,"T":2,"s":"BTCUSDT","U":200,"u":205,"pu":190,"b":[["60000.00","2.0"]],"a":[["60001.00","3.0"]]}})"},
        {1750000100, 300000000,
         R"({"stream":"btcusdt@depth@100ms","data":{"e":"depthUpdate","E":3,"T":3,"s":"BTCUSDT","U":211,"u":215,"pu":210,"b":[["59000.00","7.0"]],"a":[["59001.00","6.0"]]}})"},
        {1750000100, 400000000,
         R"({"stream":"btcusdt@depth@100ms","data":{"e":"depthUpdate","E":4,"T":4,"s":"BTCUSDT","U":216,"u":218,"pu":215,"b":[],"a":[["59002.00","5.0"]]}})"},
    };

    std::vector<Msg> tape = {
        {1750000000, 100000000,
         R"({"stream":"btcusdt@depth5@100ms","data":{"lastUpdateId":99,"bids":[["25000.00","1.0"],["24999.50","2.0"]],"asks":[["25000.50","1.5"],["25001.00","3.0"]]}})"},
        {1750000000, 200000000,
         R"({"stream":"btcusdt@depth@100ms","data":{"e":"depthUpdate","E":1,"s":"BTCUSDT","U":100,"u":101,"b":[["25000.00","1.2"],["24999.00","5.0"]],"a":[["25000.50","0.7"]]}})"},
        {1750000000, 300000000,
         R"({"stream":"btcusdt@trade","data":{"e":"trade","E":2,"s":"BTCUSDT","t":42,"p":"25000.25","q":"0.05","m":true}})"},
        {1750000000, 400000000,
         R"({"stream":"btcusdt@depth@100ms","data":{"e":"depthUpdate","E":3,"s":"BTCUSDT","U":102,"u":104,"b":[["25000.00","0.00000000"]],"a":[["25002.00","4.0"]]}})"},
        // GAP: next U should be 105, we send 300.
        {1750000000, 500000000,
         R"({"stream":"btcusdt@depth@100ms","data":{"e":"depthUpdate","E":4,"s":"BTCUSDT","U":300,"u":305,"b":[["24000.00","9.0"]],"a":[["24001.00","8.0"]]}})"},
        {1750000000, 600000000,
         R"({"stream":"btcusdt@depth@100ms","data":{"e":"depthUpdate","E":5,"s":"BTCUSDT","U":306,"u":307,"b":[["24000.50","1.0"]],"a":[]}})"},
    };

    if (venue == Venue::Usdm) tape = futures_tape;

    uint64_t conn_seq = 0;
    for (const Msg& m : tape) {
        ++metrics.messages_received;
        auto ev = parser.parse_live_message(m.raw, SplitTime{m.tsec, m.tnsec});
        if (!ev) { ++metrics.parse_errors; continue; }
        md.write(*ev, 0, 0, conn_seq++);
        ++metrics.market_data_rows;
        pipeline.process(*ev);
    }
    md.flush_all();
    pipeline.flush_all();
    metrics.print(stdout);
    return 0;
}
