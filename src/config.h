#pragma once

#include <optional>
#include <string>
#include <vector>

namespace blc {

enum class Venue { Spot, Usdm };

const char* venue_name(Venue v);                       // "spot" / "usdm"
std::optional<Venue> venue_from_string(const std::string& s);

struct Config {
    Venue venue = Venue::Spot;
    std::vector<std::string> symbols;   // uppercase, e.g. "BTCUSDT"
    std::string output_dir = "./output";
    int duration_sec = 0;               // 0 = run until Ctrl-C
    std::string replay_file;            // non-empty => replay mode, no network

    bool replay_mode() const { return !replay_file.empty(); }
};

// Parses argv. On error prints usage to stderr and returns nullopt.
std::optional<Config> parse_args(int argc, char** argv);

void print_usage(const char* prog);

}  // namespace blc
