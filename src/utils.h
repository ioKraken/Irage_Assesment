#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace blc {

// A wall-clock timestamp split into whole seconds and the nanosecond
// remainder, exactly as the CSV schema wants it.
struct SplitTime {
    int64_t tsec = 0;   // seconds since Unix epoch
    int32_t tnsec = 0;  // [0, 999'999'999]
};

// Current wall-clock time (CLOCK_REALTIME), already split.
SplitTime now_split();

// Parse a Binance decimal string (e.g. "25123.45000000") into a scaled

std::optional<int64_t> scale_decimal(std::string_view s);

// Fixed scale shared by prices and quantities: 10^8.
constexpr int64_t kScale = 100'000'000;
constexpr int kScaleDigits = 8;

// "btcusdt" -> "BTCUSDT" and back. ASCII only, which is all Binance uses.
std::string to_upper(std::string_view s);
std::string to_lower(std::string_view s);

// Format a SplitTime's date part as "YYYY-MM-DD" in UTC (for file names).
std::string utc_date_string(int64_t tsec);

}  // namespace blc
