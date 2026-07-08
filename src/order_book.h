#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <vector>

namespace blc {

// One price level change coming from a depthUpdate: absolute quantity

struct LevelUpdate {
    int64_t price = 0;  // scaled by 10^8
    int64_t qty = 0;    // scaled by 10^8
};

// Top-5 view of one side, best-first, zero-padded when fewer than 5 levels.
struct TopLevels {
    std::array<int64_t, 5> price{};
    std::array<int64_t, 5> size{};
};

class OrderBook {
public:
    // Result of applying a diff: how many levels touched each side.

    struct ApplyResult {
        int bid_changes = 0;
        int ask_changes = 0;
    };

    ApplyResult apply_diff(const std::vector<LevelUpdate>& bids,
                           const std::vector<LevelUpdate>& asks);

    // Forget everything (used on start, gap, and reconnect).
    void clear();

    bool empty() const { return bids_.empty() && asks_.empty(); }
    size_t bid_depth() const { return bids_.size(); }
    size_t ask_depth() const { return asks_.size(); }

    TopLevels top_bids() const;
    TopLevels top_asks() const;

private:
    // price -> qty, both scaled ints. Zero-qty levels are never stored.
    std::map<int64_t, int64_t> bids_;
    std::map<int64_t, int64_t> asks_;

    static void apply_side(std::map<int64_t, int64_t>& side,
                           const std::vector<LevelUpdate>& updates,
                           int& changes);
};

}  // namespace blc
