#include "config.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "utils.h"

namespace blc {

const char* venue_name(Venue v) {
    return v == Venue::Spot ? "spot" : "usdm";
}

std::optional<Venue> venue_from_string(const std::string& s) {
    if (s == "spot") return Venue::Spot;
    if (s == "usdm") return Venue::Usdm;
    return std::nullopt;
}

void print_usage(const char* prog) {
    std::fprintf(stderr,
        "Usage:\n"
        "  live capture:  %s --venue spot|usdm --symbols BTCUSDT[,ETHUSDT...]\n"
        "                 [--output-dir DIR] [--duration SECONDS]\n"
        "  replay:        %s --replay market_data_file.csv [--symbols LIST]\n"
        "                 [--output-dir DIR]\n",
        prog, prog);
}

static std::vector<std::string> split_symbols(const std::string& list) {
    std::vector<std::string> out;
    std::string current;
    for (char c : list) {
        if (c == ',') {
            if (!current.empty()) out.push_back(to_upper(current));
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) out.push_back(to_upper(current));
    return out;
}

std::optional<Config> parse_args(int argc, char** argv) {
    Config cfg;
    bool venue_given = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto next = [&]() -> const char* {
            return (i + 1 < argc) ? argv[++i] : nullptr;
        };

        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return std::nullopt;
        } else if (arg == "--venue") {
            const char* v = next();
            if (!v) { print_usage(argv[0]); return std::nullopt; }
            auto parsed = venue_from_string(v);
            if (!parsed) {
                std::fprintf(stderr, "unknown venue '%s' (use spot or usdm)\n", v);
                return std::nullopt;
            }
            cfg.venue = *parsed;
            venue_given = true;
        } else if (arg == "--symbols") {
            const char* s = next();
            if (!s) { print_usage(argv[0]); return std::nullopt; }
            cfg.symbols = split_symbols(s);
        } else if (arg == "--output-dir") {
            const char* d = next();
            if (!d) { print_usage(argv[0]); return std::nullopt; }
            cfg.output_dir = d;
        } else if (arg == "--duration") {
            const char* d = next();
            if (!d) { print_usage(argv[0]); return std::nullopt; }
            cfg.duration_sec = std::atoi(d);
            if (cfg.duration_sec < 0) cfg.duration_sec = 0;
        } else if (arg == "--replay") {
            const char* f = next();
            if (!f) { print_usage(argv[0]); return std::nullopt; }
            cfg.replay_file = f;
        } else {
            std::fprintf(stderr, "unknown argument '%s'\n", arg.c_str());
            print_usage(argv[0]);
            return std::nullopt;
        }
    }

    if (!cfg.replay_mode()) {
        if (!venue_given || cfg.symbols.empty()) {
            print_usage(argv[0]);
            return std::nullopt;
        }
        // Keep the combined-stream URL to a sane size (security/ops baseline:
        // avoid runaway memory / absurd URLs if misconfigured).
        if (cfg.symbols.size() > 10) {
            std::fprintf(stderr, "too many symbols (max 10 per run)\n");
            return std::nullopt;
        }
    }
    return cfg;
}

}  // namespace blc
