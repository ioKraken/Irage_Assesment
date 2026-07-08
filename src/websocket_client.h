#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "config.h"
#include "metrics.h"

namespace blc {


class WebSocketClient {
public:
    struct Endpoint {
        std::string host;
        std::string port;
        std::string target;  // "/stream?streams=..."
    };

    // Builds the combined-stream URL for the venue + symbols, using the

    static Endpoint make_endpoint(Venue venue,
                                  const std::vector<std::string>& symbols);

   
    void run(const Endpoint& ep,
             const std::function<void(std::string_view msg, uint32_t conn_epoch,
                                      uint64_t conn_seq)>& on_message,
             const std::function<bool()>& should_stop,
             const std::function<void()>& on_reconnect,
             Metrics& metrics);

    static constexpr uint32_t kShardId = 0;  // single connection per run
};

}  // namespace blc
