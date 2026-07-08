#include <csignal>
#include <cstdio>
#include <filesystem>

#include "binance_parser.h"
#include "book_pipeline.h"
#include "config.h"
#include "market_data_writer.h"
#include "metrics.h"
#include "replay_engine.h"
#include "utils.h"
#include "websocket_client.h"

namespace {

// The only piece of global state: a signal flag. Signal handlers may only
// touch lock-free sig_atomic_t objects, so this cannot live inside a class.
volatile std::sig_atomic_t g_stop = 0;
void handle_signal(int) { g_stop = 1; }

}  // namespace

int main(int argc, char** argv) {
    auto cfg_opt = blc::parse_args(argc, argv);
    if (!cfg_opt) return 1;
    const blc::Config& cfg = *cfg_opt;

    std::error_code ec;
    std::filesystem::create_directories(cfg.output_dir, ec);
    if (ec) {
        std::fprintf(stderr, "cannot create output dir %s: %s\n",
                     cfg.output_dir.c_str(), ec.message().c_str());
        return 1;
    }

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    blc::Metrics metrics;
    blc::BinanceParser parser;

    if (cfg.replay_mode()) {
    
        auto venue = blc::ReplayEngine::detect_venue(cfg.replay_file);
        if (!venue) {
            std::fprintf(stderr, "[replay] cannot detect venue from %s\n",
                         cfg.replay_file.c_str());
            return 1;
        }
        std::fprintf(stderr, "[replay] regenerating order book from %s (%s)\n",
                     cfg.replay_file.c_str(), blc::venue_name(*venue));
        blc::BookPipeline pipeline(cfg.output_dir, *venue, cfg.symbols, metrics);
        blc::ReplayEngine replay(parser, pipeline, metrics);
        bool ok = replay.run(cfg.replay_file);
        metrics.print(stderr);
        return ok ? 0 : 1;
    }

    // ---- live capture ----
    blc::BookPipeline pipeline(cfg.output_dir, cfg.venue, cfg.symbols, metrics);
    blc::MarketDataWriter md_writer(cfg.output_dir, cfg.venue);
    blc::WebSocketClient client;
    auto endpoint = blc::WebSocketClient::make_endpoint(cfg.venue, cfg.symbols);

    const int64_t deadline =
        cfg.duration_sec > 0 ? blc::now_split().tsec + cfg.duration_sec : 0;

    auto should_stop = [&]() -> bool {
        if (g_stop) return true;
        return deadline != 0 && blc::now_split().tsec >= deadline;
    };

    // The whole pipeline for one message runs inside this callback, on the
    // single network thread: stamp time -> parse -> market-data row ->
    // book update -> orderbook row. That serialization is exactly the
    // "rows reflect processing order" guarantee.
    //
    // Periodic flush: RAII already flushes on every clean exit, but a hard
    // kill (SIGKILL, power loss) cannot run destructors. Flushing every 5
    // seconds bounds worst-case data loss to 5 s instead of a full 1 MiB
    // buffer, at a negligible cost (a couple of syscalls per 5 s).
    int64_t last_flush_tsec = blc::now_split().tsec;
    auto on_message = [&](std::string_view msg, uint32_t conn_epoch,
                          uint64_t conn_seq) {
        blc::SplitTime recv = blc::now_split();
        std::optional<blc::Event> ev = parser.parse_live_message(msg, recv);
        if (!ev) {
            ++metrics.parse_errors;
            return;
        }
        md_writer.write(*ev, blc::WebSocketClient::kShardId, conn_epoch,
                        conn_seq);
        ++metrics.market_data_rows;
        pipeline.process(*ev);

        if (recv.tsec - last_flush_tsec >= 5) {
            md_writer.flush_all();
            pipeline.flush_all();
            last_flush_tsec = recv.tsec;
        }
    };

    auto on_reconnect = [&]() { pipeline.on_reconnect(); };

    client.run(endpoint, on_message, should_stop, on_reconnect, metrics);

    md_writer.flush_all();
    pipeline.flush_all();
    metrics.print(stderr);
    std::fprintf(stderr, "[main] shutdown complete\n");
    return 0;
}
