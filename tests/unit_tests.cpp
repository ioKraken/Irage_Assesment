// Dependency-free unit tests (plain asserts) for every offline module:
// decimal scaling, CSV escaping round-trip, order book semantics, and the
// Binance parser fed with hand-written payloads for both venues.
#undef NDEBUG
#include <cassert>
#include <cstdio>

#include "binance_parser.h"
#include "csv_writer.h"
#include "order_book.h"
#include "utils.h"

using namespace blc;

static void test_scale_decimal() {
    assert(scale_decimal("0").value() == 0);
    assert(scale_decimal("1").value() == 100'000'000);
    assert(scale_decimal("0.5").value() == 50'000'000);
    assert(scale_decimal("25000.12345678").value() == 2'500'012'345'678LL);
    assert(scale_decimal("0.00000001").value() == 1);
    // truncation beyond 8 fraction digits is deterministic
    assert(scale_decimal("0.123456789").value() == 12'345'678);
    assert(scale_decimal("100.").value() == 10'000'000'000LL);
    assert(!scale_decimal("").has_value());
    assert(!scale_decimal("abc").has_value());
    assert(!scale_decimal("1.2.3").has_value());
    assert(!scale_decimal("99999999999999999999").has_value());  // overflow
    std::puts("scale_decimal: OK");
}

static void test_csv_roundtrip() {
    std::string row;
    append_csv_field(row, "plain");
    row.push_back(',');
    append_csv_field(row, R"({"e":"trade","p":"1,5","q":"a\"b"})");
    row.push_back(',');
    append_csv_field(row, "line\nbreak");

    auto fields = parse_csv_line(row);
    assert(fields.size() == 3);
    assert(fields[0] == "plain");
    assert(fields[1] == R"({"e":"trade","p":"1,5","q":"a\"b"})");
    assert(fields[2] == "line\nbreak");

    // CRLF tolerance: trailing \r outside quotes is dropped.
    auto crlf = parse_csv_line("a,b\r");
    assert(crlf.size() == 2 && crlf[0] == "a" && crlf[1] == "b");

    // Empty fields survive: ",," is three empty fields.
    auto empties = parse_csv_line(",,");
    assert(empties.size() == 3);
    assert(empties[0].empty() && empties[1].empty() && empties[2].empty());

    // A quoted field containing \r keeps it (RFC 4180).
    auto quoted_cr = parse_csv_line("\"x\ry\",z");
    assert(quoted_cr.size() == 2 && quoted_cr[0] == "x\ry");
    std::puts("csv round-trip: OK");
}

static void test_order_book() {
    OrderBook book;

    // Build a small book.
    book.apply_diff(
        {{100, 10}, {99, 20}, {98, 30}},   // bids
        {{101, 11}, {102, 21}, {103, 31}}  // asks
    );
    TopLevels b = book.top_bids();
    assert(b.price[0] == 100 && b.size[0] == 10);  // best bid first
    assert(b.price[1] == 99);
    assert(b.price[3] == 0 && b.size[3] == 0);     // zero padding
    TopLevels a = book.top_asks();
    assert(a.price[0] == 101);                     // best ask = lowest

    // Absolute replacement, not increment.
    book.apply_diff({{100, 5}}, {});
    assert(book.top_bids().size[0] == 5);

    // qty 0 removes; next best becomes top.
    book.apply_diff({{100, 0}}, {});
    assert(book.top_bids().price[0] == 99);

    // Removing an unknown level is a silent no-op.
    book.apply_diff({{55, 0}}, {});
    assert(book.bid_depth() == 2);

    // Side attribution.
    auto r = book.apply_diff({{97, 7}}, {});
    assert(r.bid_changes == 1 && r.ask_changes == 0);

    book.clear();
    assert(book.empty());
    TopLevels empty = book.top_bids();
    assert(empty.price[0] == 0 && empty.size[0] == 0);
    std::puts("order_book: OK");
}

static void test_parser_spot() {
    BinanceParser parser;
    SplitTime t{1750000000, 123456789};

    // Spot diff depth inside the combined-stream envelope.
    const char* diff =
        R"({"stream":"btcusdt@depth@100ms","data":{"e":"depthUpdate","E":1,)"
        R"("s":"BTCUSDT","U":100,"u":102,)"
        R"("b":[["25000.10","1.5"],["24999.00","0.00000000"]],)"
        R"("a":[["25001.00","2"]]}})";
    auto ev = parser.parse_live_message(diff, t);
    assert(ev.has_value());
    assert(ev->kind == StreamKind::DepthDiff);
    assert(ev->symbol == "BTCUSDT");
    assert(ev->depth_diff.has_value());
    assert(ev->depth_diff->first_update_id == 100);
    assert(ev->depth_diff->final_update_id == 102);
    assert(!ev->depth_diff->prev_final_id.has_value());  // spot has no pu
    assert(ev->depth_diff->bids.size() == 2);
    assert(ev->depth_diff->bids[0].price == 2'500'010'000'000LL);
    assert(ev->depth_diff->bids[1].qty == 0);  // removal marker
    assert(ev->payload.find("\"stream\"") == std::string::npos);  // envelope gone
    assert(ev->payload.find('\n') == std::string::npos);          // minified

    // Spot depth5 has bids/asks keys and NO "e" field.
    const char* d5 =
        R"({"stream":"btcusdt@depth5@100ms","data":{"lastUpdateId":50,)"
        R"("bids":[["25000.00","1"]],"asks":[["25001.00","1"]]}})";
    auto ev5 = parser.parse_live_message(d5, t);
    assert(ev5.has_value() && ev5->kind == StreamKind::Depth5);
    assert(ev5->depth5->bids.size() == 1);

    // Trade parses fine, carries no typed data.
    const char* trade =
        R"({"stream":"btcusdt@trade","data":{"e":"trade","p":"25000.5","q":"0.1","m":true}})";
    auto evt = parser.parse_live_message(trade, t);
    assert(evt.has_value() && evt->kind == StreamKind::Trade);
    assert(!evt->depth_diff && !evt->depth5);

    // Garbage is rejected, not UB.
    assert(!parser.parse_live_message("not json", t).has_value());
    assert(!parser.parse_live_message(R"({"stream":"x","data":{}})", t).has_value());
    std::puts("parser (spot): OK");
}

static void test_parser_futures_and_replay_path() {
    BinanceParser parser;
    SplitTime t{1750000001, 0};

    // Futures diff carries pu; futures depth5 looks like a depthUpdate.
    const char* fut =
        R"({"stream":"btcusdt@depth@100ms","data":{"e":"depthUpdate",)"
        R"("E":2,"T":2,"s":"BTCUSDT","U":200,"u":205,"pu":199,)"
        R"("b":[["60000.00","3"]],"a":[]}})";
    auto ev = parser.parse_live_message(fut, t);
    assert(ev.has_value());
    assert(ev->depth_diff->prev_final_id.has_value());
    assert(*ev->depth_diff->prev_final_id == 199);

    // Replay path: typed data recovered from the stored payload alone.
    Event replayed;
    replayed.recv = t;
    replayed.kind = StreamKind::DepthDiff;
    replayed.symbol = "BTCUSDT";
    replayed.payload = ev->payload;
    assert(parser.parse_payload_into(replayed));
    assert(replayed.depth_diff->final_update_id == 205);
    assert(replayed.depth_diff->bids[0].price ==
           ev->depth_diff->bids[0].price);
    std::puts("parser (futures) + replay path: OK");
}

int main() {
    test_scale_decimal();
    test_csv_roundtrip();
    test_order_book();
    test_parser_spot();
    test_parser_futures_and_replay_path();
    std::puts("ALL TESTS PASSED");
    return 0;
}
