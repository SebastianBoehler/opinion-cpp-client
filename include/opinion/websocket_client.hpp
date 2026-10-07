#pragma once

#include "opinion/environment.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace opinion
{
    namespace channel
    {
        inline constexpr std::string_view kDepthDiff = "market.depth.diff";
        inline constexpr std::string_view kLastPrice = "market.last.price";
        inline constexpr std::string_view kLastTrade = "market.last.trade";
        inline constexpr std::string_view kOrderUpdate = "trade.order.update";
        inline constexpr std::string_view kTradeRecord = "trade.record.new";
    } // namespace channel

    // Binary markets send marketId. Categorical subscriptions send rootMarketId
    // and omit marketId (docs: if rootMarketId is defined, marketId is omitted).
    // market.depth.diff is binary-only; pass market_id.
    struct WsSubscription
    {
        std::string channel;
        std::optional<std::int64_t> market_id;
        std::optional<std::int64_t> root_market_id;
    };

    struct WsMessage
    {
        std::string msg_type;
        std::string raw;
    };

    std::string build_ws_url(std::string_view base_url, std::string_view api_key);
    std::string build_heartbeat_message();
    std::string build_channel_message(std::string_view action, const WsSubscription &subscription);

    // Native WebSocket client for wss://ws.opinion.trade?apikey=...
    // Sends {"action":"HEARTBEAT"} on a timer (docs: about every 30 seconds).
    // Subscriptions are replayed after a reconnect.
    class WebSocketClient
    {
    public:
        using MessageCallback = std::function<void(const WsMessage &)>;
        using ErrorCallback = std::function<void(const std::string &)>;
        using VoidCallback = std::function<void()>;

        WebSocketClient(std::string websocket_url, std::string api_key);
        explicit WebSocketClient(const Environment &environment, std::string api_key);
        ~WebSocketClient();

        WebSocketClient(const WebSocketClient &) = delete;
        WebSocketClient &operator=(const WebSocketClient &) = delete;

        void set_message_callback(MessageCallback callback);
        void set_error_callback(ErrorCallback callback);
        void set_open_callback(VoidCallback callback);
        void set_heartbeat_interval(std::chrono::seconds interval);
        void set_reconnect(bool enabled);

        void connect();
        void close();
        bool connected() const;

        void subscribe(const WsSubscription &subscription);
        void unsubscribe(const WsSubscription &subscription);

    private:
        struct Impl;
        Impl *impl_;
    };

    // Market-channel helper. Depth diffs are per binary market id.
    class MarketStream
    {
    public:
        MarketStream(const Environment &environment, std::string api_key);
        ~MarketStream();

        MarketStream(const MarketStream &) = delete;
        MarketStream &operator=(const MarketStream &) = delete;

        WebSocketClient &socket() { return socket_; }

        void connect();
        void close();

        void subscribe_depth(std::int64_t market_id);
        void subscribe_last_price(std::int64_t market_id);
        void subscribe_last_price_root(std::int64_t root_market_id);
        void subscribe_last_trade(std::int64_t market_id);
        void subscribe_last_trade_root(std::int64_t root_market_id);

    private:
        WebSocketClient socket_;
    };
} // namespace opinion
