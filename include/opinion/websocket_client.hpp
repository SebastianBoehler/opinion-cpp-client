#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace opinion
{
    enum class WsState
    {
        Disconnected,
        Connecting,
        Connected,
        Closing
    };

    struct WebSocketOptions
    {
        bool auto_reconnect{true};
        int ping_interval_seconds{30};
        // Application HEARTBEAT interval. The docs ask for one about every 30s.
        int heartbeat_interval_seconds{30};
        // This IXWebSocket transport rejects nonempty routes before connecting.
        std::string proxy_url;
        std::string interface_name;
    };

    // Native WebSocket skeleton for wss://ws.opinion.trade.
    // Authentication is the `apikey` query parameter, not an HTTP header.
    class WebSocketClient
    {
    public:
        using MessageCallback = std::function<void(const std::string &)>;
        using VoidCallback = std::function<void()>;
        using ErrorCallback = std::function<void(const std::string &)>;

        WebSocketClient();
        ~WebSocketClient();

        WebSocketClient(const WebSocketClient &) = delete;
        WebSocketClient &operator=(const WebSocketClient &) = delete;

        void configure(const WebSocketOptions &options);
        void set_url(std::string url);

        void on_message(MessageCallback callback);
        void on_open(VoidCallback callback);
        void on_close(VoidCallback callback);
        void on_error(ErrorCallback callback);

        bool connect();
        bool wait_until_connected(std::chrono::milliseconds timeout);
        void disconnect();
        bool is_connected() const;
        WsState state() const;

        bool send(const std::string &message);
        bool send_heartbeat();

        // Remembered and replayed after an automatic reconnect.
        bool subscribe(const std::string &message);
        bool unsubscribe(const std::string &message);

        static std::string subscribe_message(std::string_view channel,
                                             std::int64_t market_id,
                                             bool categorical_root);
        static std::string unsubscribe_message(std::string_view channel,
                                               std::int64_t market_id,
                                               bool categorical_root);

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

    inline constexpr const char *k_channel_depth_diff = "market.depth.diff";
    inline constexpr const char *k_channel_last_price = "market.last.price";
    inline constexpr const char *k_channel_last_trade = "market.last.trade";
    inline constexpr const char *k_channel_order_update = "trade.order.update";
    inline constexpr const char *k_channel_trade_record = "trade.record.new";
} // namespace opinion
