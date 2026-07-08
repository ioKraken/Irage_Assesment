#include "order_book.h"

namespace blc {

void OrderBook::apply_side(std::map<int64_t, int64_t>& side,
                           const std::vector<LevelUpdate>& updates,
                           int& changes) {
    for (const LevelUpdate& lu : updates) {
        if (lu.qty == 0) {
            // Absolute semantics: qty 0 removes the level. Removing a level
            side.erase(lu.price);
        } else {
            // Absolute replacement, NOT an increment.
            side[lu.price] = lu.qty;
        }
        ++changes;
    }
}

OrderBook::ApplyResult OrderBook::apply_diff(
    const std::vector<LevelUpdate>& bids,
    const std::vector<LevelUpdate>& asks) {
    ApplyResult r;
    apply_side(bids_, bids, r.bid_changes);
    apply_side(asks_, asks, r.ask_changes);
    return r;
}

void OrderBook::clear() {
    bids_.clear();
    asks_.clear();
}

TopLevels OrderBook::top_bids() const {
    TopLevels t;  
    int i = 0;
    // Best bid = highest price => iterate the map backwards.
    for (auto it = bids_.rbegin(); it != bids_.rend() && i < 5; ++it, ++i) {
        t.price[static_cast<size_t>(i)] = it->first;
        t.size[static_cast<size_t>(i)] = it->second;
    }
    return t;
}

TopLevels OrderBook::top_asks() const {
    TopLevels t;
    int i = 0;
    // Best ask = lowest price => iterate the map forwards.
    for (auto it = asks_.begin(); it != asks_.end() && i < 5; ++it, ++i) {
        t.price[static_cast<size_t>(i)] = it->first;
        t.size[static_cast<size_t>(i)] = it->second;
    }
    return t;
}

} 
