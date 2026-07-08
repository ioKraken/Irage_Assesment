#include "websocket_client.h"

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>

#include <chrono>
#include <cstdio>
#include <thread>

#include "utils.h"

namespace blc {

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ssl = net::ssl;
using tcp = net::ip::tcp;

WebSocketClient::Endpoint WebSocketClient::make_endpoint(
    Venue venue, const std::vector<std::string>& symbols) {
    Endpoint ep;
    if (venue == Venue::Spot) {
        ep.host = "stream.binance.com";
        ep.port = "9443";
        ep.target = "/stream?streams=";
    } else {
        ep.host = "fstream.binance.com";
        ep.port = "443";
        ep.target = "/public/stream?streams=";  // per assignment PDF
    }
    bool first = true;
    for (const std::string& sym : symbols) {
        std::string lower = to_lower(sym);
        for (const char* suffix : {"@depth@100ms", "@depth5@100ms", "@trade"}) {
            if (!first) ep.target.push_back('/');
            ep.target += lower + suffix;
            first = false;
        }
    }
    return ep;
}

namespace {

// One connection lifetime: connect, TLS handshake, WS handshake, read until
// stop/error. Returns true if we stopped on purpose, false on error (caller
// then reconnects). All the objects are stack-local, so RAII tears down the
// socket in every exit path.
bool run_one_connection(
    const WebSocketClient::Endpoint& ep, uint32_t conn_epoch,
    const std::function<void(std::string_view, uint32_t, uint64_t)>& on_message,
    const std::function<bool()>& should_stop, Metrics& metrics) {
    try {
        net::io_context ioc;

        ssl::context ssl_ctx(ssl::context::tls_client);
        ssl_ctx.set_default_verify_paths();               // system CA bundle
        ssl_ctx.set_verify_mode(ssl::verify_peer);        // TLS verification ON

        tcp::resolver resolver(ioc);
        auto endpoints = resolver.resolve(ep.host, ep.port);

        websocket::stream<beast::ssl_stream<tcp::socket>> ws(ioc, ssl_ctx);

        // SNI is required by Binance's TLS endpoints.
        if (!SSL_set_tlsext_host_name(ws.next_layer().native_handle(),
                                      ep.host.c_str())) {
            std::fprintf(stderr, "[ws] failed to set SNI hostname\n");
            return false;
        }

        net::connect(beast::get_lowest_layer(ws), endpoints);
        ws.next_layer().handshake(ssl::stream_base::client);

        // Host header must include the port when it is not the default 443.
        std::string host_header =
            ep.port == "443" ? ep.host : ep.host + ":" + ep.port;
        ws.handshake(host_header, ep.target);

        std::fprintf(stderr, "[ws] connected (epoch %u): wss://%s%s\n",
                     conn_epoch, host_header.c_str(), ep.target.c_str());

        beast::flat_buffer buffer;
        uint64_t conn_seq = 0;  // monotonic within (shard_id, conn_epoch)

        while (!should_stop()) {
            buffer.clear();
            ws.read(buffer);  // blocks until a full message arrives
            ++metrics.messages_received;

            // flat_buffer is contiguous: hand out a zero-copy view.
            std::string_view msg(
                static_cast<const char*>(buffer.data().data()),
                buffer.data().size());
            on_message(msg, conn_epoch, conn_seq++);
        }

        // Graceful close; ignore errors, we're leaving anyway.
        beast::error_code ec;
        ws.close(websocket::close_code::normal, ec);
        return true;
    } catch (const std::exception& e) {
        if (should_stop()) return true;  // error caused by our own shutdown
        std::fprintf(stderr, "[ws] connection error (epoch %u): %s\n",
                     conn_epoch, e.what());
        return false;
    }
}

}  // namespace

void WebSocketClient::run(
    const Endpoint& ep,
    const std::function<void(std::string_view, uint32_t, uint64_t)>& on_message,
    const std::function<bool()>& should_stop,
    const std::function<void()>& on_reconnect, Metrics& metrics) {
    uint32_t conn_epoch = 0;
    int backoff_sec = 1;

    while (!should_stop()) {
        if (conn_epoch > 0) {
            ++metrics.reconnects;
            on_reconnect();  // let the pipeline reset its books first
        }

        auto started = std::chrono::steady_clock::now();
        bool clean_exit =
            run_one_connection(ep, conn_epoch, on_message, should_stop, metrics);
        if (clean_exit) break;

        // A connection that lived a while means the network is basically
        // healthy: start the backoff over from 1 s.
        if (std::chrono::steady_clock::now() - started >
            std::chrono::seconds(10)) {
            backoff_sec = 1;
        }

        ++conn_epoch;

        // Exponential backoff, capped, so we don't hammer Binance.
        std::fprintf(stderr, "[ws] reconnecting in %d s...\n", backoff_sec);
        for (int slept = 0; slept < backoff_sec && !should_stop(); ++slept) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        backoff_sec = std::min(backoff_sec * 2, 30);
    }
}

}  // namespace blc
