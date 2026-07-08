#include "binance_parser.h"
#include <cstdio>
#include "simdjson.h"
#include <initializer_list>
#include <utility>

namespace blc {

const char* stream_kind_name(StreamKind k) {
    switch (k) {
        case StreamKind::DepthDiff: return "depth_diff";
        case StreamKind::Depth5:    return "depth5";
        case StreamKind::Trade:     return "trade";
    }
    return "unknown";
}

std::optional<StreamKind> stream_kind_from_string(std::string_view s) {
    if (s == "depth_diff") return StreamKind::DepthDiff;
    if (s == "depth5")     return StreamKind::Depth5;
    if (s == "trade")      return StreamKind::Trade;
    return std::nullopt;
}

// ---------------------------------------------------------------------------

struct BinanceParser::Impl {
    // One long-lived parser: simdjson reuses its internal buffers across
    // parse() calls, so steady-state parsing does not allocate per message.
    simdjson::dom::parser parser;
};

BinanceParser::BinanceParser() : impl_(std::make_unique<Impl>()) {}
BinanceParser::~BinanceParser() = default;

namespace {

std::optional<std::pair<std::string, StreamKind>> classify_stream(
    std::string_view stream) {
    size_t at = stream.find('@');
    if (at == std::string_view::npos || at == 0) return std::nullopt;

    std::string symbol = to_upper(stream.substr(0, at));
    std::string_view rest = stream.substr(at + 1);

    if (rest.rfind("depth5", 0) == 0) return {{symbol, StreamKind::Depth5}};
    if (rest.rfind("depth", 0) == 0)  return {{symbol, StreamKind::DepthDiff}};
    if (rest.rfind("trade", 0) == 0)  return {{symbol, StreamKind::Trade}};
    return std::nullopt;
}

// Reads a Binance level array like [["25000.10","1.5"], ...] into scaled ints.
bool read_levels(simdjson::dom::element arr, std::vector<LevelUpdate>& out) {
    simdjson::dom::array levels;
    if (arr.get_array().get(levels) != simdjson::SUCCESS) return false;
    out.reserve(levels.size());
    for (simdjson::dom::element level : levels) {
        simdjson::dom::array pair;
        if (level.get_array().get(pair) != simdjson::SUCCESS) return false;
        std::string_view price_s, qty_s;
        auto it = pair.begin();
        if (it == pair.end() || (*it).get_string().get(price_s) != simdjson::SUCCESS)
            return false;
        ++it;
        if (it == pair.end() || (*it).get_string().get(qty_s) != simdjson::SUCCESS)
            return false;
        auto price = scale_decimal(price_s);
        auto qty = scale_decimal(qty_s);
        if (!price || !qty) return false;
        out.push_back(LevelUpdate{*price, *qty});
    }
    return true;
}

// Tries a list of keys and returns the first that exists (spot depth5 says
// "bids"/"asks", everything else says "b"/"a").
bool read_levels_any_key(simdjson::dom::element obj,
                         std::initializer_list<const char*> keys,
                         std::vector<LevelUpdate>& out) {
    for (const char* key : keys) {
        simdjson::dom::element found;
        if (obj[key].get(found) == simdjson::SUCCESS) {
            return read_levels(found, out);
        }
    }
    return false;
}

bool extract_typed(StreamKind kind, simdjson::dom::element data, Event& ev) {
    switch (kind) {
        case StreamKind::Trade:
            return true;  // trades never touch the book: no typed data needed
        case StreamKind::DepthDiff: {
            DepthDiffData d;
            int64_t U = 0, u = 0;
            if (data["U"].get_int64().get(U) != simdjson::SUCCESS) return false;
            if (data["u"].get_int64().get(u) != simdjson::SUCCESS) return false;
            d.first_update_id = U;
            d.final_update_id = u;
            int64_t pu = 0;
            if (data["pu"].get_int64().get(pu) == simdjson::SUCCESS) {
                d.prev_final_id = pu;  // present on USD-M futures only
            }
            if (!read_levels_any_key(data, {"b"}, d.bids)) return false;
            if (!read_levels_any_key(data, {"a"}, d.asks)) return false;
            ev.depth_diff = std::move(d);
            return true;
        }
        case StreamKind::Depth5: {
            Depth5Data d;
            if (!read_levels_any_key(data, {"bids", "b"}, d.bids)) return false;
            if (!read_levels_any_key(data, {"asks", "a"}, d.asks)) return false;
            ev.depth5 = std::move(d);
            return true;
        }
    }
    return false;
}

}  // namespace

std::optional<Event> BinanceParser::parse_live_message(std::string_view msg,
                                                       SplitTime recv) {
    simdjson::dom::element doc;
    if (impl_->parser.parse(msg.data(), msg.size()).get(doc) !=
        simdjson::SUCCESS) {
        return std::nullopt;
    }

    std::string_view stream;
    simdjson::dom::element data;
    if (doc["stream"].get_string().get(stream) != simdjson::SUCCESS) return std::nullopt;
    if (doc["data"].get(data) != simdjson::SUCCESS) return std::nullopt;

    auto classified = classify_stream(stream);
    if (!classified) return std::nullopt;

    Event ev;
    ev.recv = recv;
    ev.symbol = std::move(classified->first);
    ev.kind = classified->second;
    ev.payload = simdjson::to_string(data);  // minified inner JSON

    if (!extract_typed(ev.kind, data, ev)) return std::nullopt;
    return ev;
}

bool BinanceParser::parse_payload_into(Event& event) {
    simdjson::dom::element data;
    if (impl_->parser.parse(event.payload.data(), event.payload.size())
            .get(data) != simdjson::SUCCESS) {
        return false;
    }
    return extract_typed(event.kind, data, event);
}

}  // namespace blc
