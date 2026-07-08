#include "utils.h"

#include <cctype>
#include <ctime>

namespace blc {

SplitTime now_split() {
    timespec ts{};
    clock_gettime(CLOCK_REALTIME, &ts);
    return SplitTime{static_cast<int64_t>(ts.tv_sec),
                     static_cast<int32_t>(ts.tv_nsec)};
}

std::optional<int64_t> scale_decimal(std::string_view s) {
    if (s.empty()) return std::nullopt;

    size_t i = 0;
    bool negative = false;
    if (s[0] == '-') {  // Binance prices/sizes are non-negative, but be safe.
        negative = true;
        i = 1;
        if (s.size() == 1) return std::nullopt;
    }

    // Integer part.
    int64_t value = 0;
    bool any_digit = false;
    for (; i < s.size() && s[i] != '.'; ++i) {
        char c = s[i];
        if (c < '0' || c > '9') return std::nullopt;
        any_digit = true;
        // Overflow check before multiplying: value*10 + d must fit.
        if (value > (INT64_MAX - (c - '0')) / 10) return std::nullopt;
        value = value * 10 + (c - '0');
    }

   
    int frac_digits = 0;
    int64_t frac = 0;
    if (i < s.size() && s[i] == '.') {
        ++i;
        for (; i < s.size(); ++i) {
            char c = s[i];
            if (c < '0' || c > '9') return std::nullopt;
            any_digit = true;
            if (frac_digits < kScaleDigits) {
                frac = frac * 10 + (c - '0');
                ++frac_digits;
            }
            // digits beyond the 8th are truncated deterministically
        }
    }
    if (!any_digit) return std::nullopt;

    // Pad fraction to exactly 8 digits: "0.5" -> frac 5, pad to 50000000.
    for (int d = frac_digits; d < kScaleDigits; ++d) frac *= 10;

    if (value > (INT64_MAX - frac) / kScale) return std::nullopt;
    int64_t result = value * kScale + frac;
    return negative ? -result : result;
}

std::string to_upper(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

std::string to_lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::string utc_date_string(int64_t tsec) {
    time_t t = static_cast<time_t>(tsec);
    tm utc{};
    gmtime_r(&t, &utc);
    char buf[32];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d", utc.tm_year + 1900,
             utc.tm_mon + 1, utc.tm_mday);
    return std::string(buf);
}

}  // namespace blc
