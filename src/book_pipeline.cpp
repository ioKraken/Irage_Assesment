#include "book_pipeline.h"

#include "utils.h"

namespace blc {

BookPipeline::BookPipeline(std::string output_dir, Venue venue,
                           const std::vector<std::string>& symbols,
                           Metrics& metrics)
    : output_dir_(std::move(output_dir)), venue_(venue), metrics_(metrics) {
    for (size_t i = 0; i < symbols.size(); ++i) {
        id_by_symbol_[symbols[i]] = static_cast<int32_t>(i);
    }
    next_unknown_id_ = static_cast<int32_t>(symbols.size());
    row_.reserve(512);
}

BookPipeline::SymbolState* BookPipeline::state_for(const std::string& symbol,
                                                   int64_t tsec) {
    auto it = states_.find(symbol);
    if (it == states_.end()) {
        SymbolState st;
        auto id_it = id_by_symbol_.find(symbol);
        if (id_it != id_by_symbol_.end()) {
            st.id = id_it->second;
        } else {
            // Replay without --symbols: assign ids in order of first
          
            st.id = next_unknown_id_++;
        }
        std::string path = output_dir_ + "/market_data_" +
                           venue_name(venue_) + "_" + symbol + "_" +
                           utc_date_string(tsec) + "_orderbook.csv";
        st.file = std::make_unique<CsvFile>(path, kHeader);
        it = states_.emplace(symbol, std::move(st)).first;
    }
    return &it->second;
}

bool BookPipeline::sequence_ok(const SymbolState& st,
                               const DepthDiffData& d) const {
    if (st.last_final_id < 0) return true;  // first diff after start/reset
    if (venue_ == Venue::Usdm && d.prev_final_id) {
        // Futures rule: this event's pu must equal the previous event's u.
        return *d.prev_final_id == st.last_final_id;
    }
    // Spot rule: this event's U must be exactly previous u + 1.
    return d.first_update_id == st.last_final_id + 1;
}

void BookPipeline::process(const Event& ev) {
    switch (ev.kind) {
        case StreamKind::Trade:
            return;  // trades don't touch a diff-based book (documented)

        case StreamKind::DepthDiff: {
            if (!ev.depth_diff) return;
            SymbolState* st = state_for(ev.symbol, ev.recv.tsec);
            const DepthDiffData& d = *ev.depth_diff;

            if (!sequence_ok(*st, d)) {
                // Gap: we lost at least one diff, so the book is no longer
                ++metrics_.sequence_gaps;
                st->book.clear();
            }
            OrderBook::ApplyResult r = st->book.apply_diff(d.bids, d.asks);
            st->last_final_id = d.final_update_id;

            char side = 'N';
            if (r.bid_changes > 0 && r.ask_changes == 0) side = 'B';
            else if (r.ask_changes > 0 && r.bid_changes == 0) side = 'S';

            emit_row(*st, ev, 'D', side, st->book.top_bids(),
                     st->book.top_asks());
            return;
        }

        case StreamKind::Depth5: {
            if (!ev.depth5) return;
            SymbolState* st = state_for(ev.symbol, ev.recv.tsec);

            // depth5 has REPLACE semantics for the top 5 only. Merging a
            // directly from the depth5 payload, type 'P'.
            TopLevels bids{}, asks{};
            const Depth5Data& d = *ev.depth5;
            for (size_t i = 0; i < d.bids.size() && i < 5; ++i) {
                bids.price[i] = d.bids[i].price;
                bids.size[i] = d.bids[i].qty;
            }
            for (size_t i = 0; i < d.asks.size() && i < 5; ++i) {
                asks.price[i] = d.asks[i].price;
                asks.size[i] = d.asks[i].qty;
            }
            emit_row(*st, ev, 'P', 'N', bids, asks);
            return;
        }
    }
}

void BookPipeline::on_reconnect() {
    // New conn_epoch: diffs may be missing across the gap, so every book is

    for (auto& [sym, st] : states_) {
        st.book.clear();
        st.last_final_id = -1;
    }
}

void BookPipeline::emit_row(SymbolState& st, const Event& ev, char type,
                            char side, const TopLevels& bids,
                            const TopLevels& asks) {
    // Timestamps deliberately reuse the receive time of the market-data row
    
    row_.clear();
    row_.append(std::to_string(ev.recv.tsec));
    row_.push_back(',');
    row_.append(std::to_string(ev.recv.tnsec));
    row_.push_back(',');
    row_.append(std::to_string(st.seq_no++));
    row_.push_back(',');
    row_.append(std::to_string(st.id));
    row_.push_back(',');
    row_.push_back(type);
    row_.push_back(',');
    row_.push_back(side);
    for (int i = 0; i < 5; ++i) {
        row_.push_back(',');
        row_.append(std::to_string(bids.price[static_cast<size_t>(i)]));
    }
    for (int i = 0; i < 5; ++i) {
        row_.push_back(',');
        row_.append(std::to_string(bids.size[static_cast<size_t>(i)]));
    }
    for (int i = 0; i < 5; ++i) {
        row_.push_back(',');
        row_.append(std::to_string(asks.price[static_cast<size_t>(i)]));
    }
    for (int i = 0; i < 5; ++i) {
        row_.push_back(',');
        row_.append(std::to_string(asks.size[static_cast<size_t>(i)]));
    }
    st.file->write_row(row_);
    ++metrics_.orderbook_rows;
}

void BookPipeline::flush_all() {
    for (auto& [sym, st] : states_) st.file->flush();
}

}  // namespace blc
