#include "opinion/websocket_client.hpp"

#include <nlohmann/json.hpp>

#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocket.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

namespace opinion
{
    namespace
    {
        std::once_flag g_net_once;

        void ensure_net()
        {
            std::call_once(g_net_once, [] { ix::initNetSystem(); });
        }

        char hex_nibble(unsigned v) { return static_cast<char>(v < 10 ? '0' + v : 'A' + (v - 10)); }
    } // namespace

    std::string build_ws_url(std::string_view base_url, std::string_view api_key)
    {
        std::string url(base_url);
        const auto q = url.find('?');
        url.push_back(q == std::string::npos ? '?' : '&');
        url += "apikey=";
        for (unsigned char c : api_key)
        {
            if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
                url.push_back(static_cast<char>(c));
            else
            {
                url.push_back('%');
                url.push_back(hex_nibble(c >> 4));
                url.push_back(hex_nibble(c & 0x0f));
            }
        }
        return url;
    }

    std::string build_heartbeat_message()
    {
        return nlohmann::json{{"action", "HEARTBEAT"}}.dump();
    }

    std::string build_channel_message(std::string_view action, const WsSubscription &subscription)
    {
        if (subscription.channel.empty())
            throw std::invalid_argument("websocket channel is required");
        nlohmann::json message = {{"action", std::string(action)}, {"channel", subscription.channel}};
        if (subscription.root_market_id)
            message["rootMarketId"] = *subscription.root_market_id;
        else if (subscription.market_id)
            message["marketId"] = *subscription.market_id;
        else
            throw std::invalid_argument("websocket subscription needs marketId or rootMarketId");
        return message.dump();
    }

    struct WebSocketClient::Impl
    {
        std::string url;
        ix::WebSocket socket;
        MessageCallback on_message;
        ErrorCallback on_error;
        VoidCallback on_open;
        std::chrono::seconds heartbeat{30};
        bool reconnect{true};
        std::atomic<bool> open{false};
        std::atomic<bool> stopping{false};
        std::mutex mu;
        std::vector<WsSubscription> subscriptions;
        std::thread heart;
        std::thread reconnect_thread;
        int attempt{0};

        explicit Impl(std::string websocket_url) : url(std::move(websocket_url)) {}

        ~Impl() { stop(); }

        void stop()
        {
            stopping = true;
            open = false;
            socket.stop();
            if (heart.joinable())
                heart.join();
            if (reconnect_thread.joinable())
                reconnect_thread.join();
        }

        void start_heart()
        {
            if (heart.joinable())
                heart.join();
            heart = std::thread([this] {
                while (!stopping.load() && open.load())
                {
                    const int ticks = static_cast<int>(heartbeat.count()) * 5;
                    for (int i = 0; i < ticks && !stopping.load() && open.load(); ++i)
                        std::this_thread::sleep_for(std::chrono::milliseconds(200));
                    if (open.load() && !stopping.load())
                        socket.send(build_heartbeat_message());
                }
            });
        }

        void schedule_reconnect()
        {
            if (reconnect_thread.joinable())
                reconnect_thread.join();
            const int attempt_now = ++attempt;
            const int delay_ms = std::min(10000, 250 * (1 << std::min(attempt_now, 5)));
            reconnect_thread = std::thread([this, delay_ms] {
                std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
                if (!stopping.load())
                    socket.start();
            });
        }

        void replay()
        {
            std::lock_guard lock(mu);
            for (const auto &sub : subscriptions)
            {
                try
                {
                    socket.send(build_channel_message("SUBSCRIBE", sub));
                }
                catch (const std::exception &ex)
                {
                    if (on_error)
                        on_error(ex.what());
                }
            }
        }
    };

    WebSocketClient::WebSocketClient(std::string websocket_url, std::string api_key)
    {
        if (api_key.empty())
            throw std::invalid_argument("websocket apikey is required");
        ensure_net();
        impl_ = new Impl(build_ws_url(websocket_url, api_key));
        impl_->socket.setPingInterval(0);
        impl_->socket.disableAutomaticReconnection();
        impl_->socket.setOnMessageCallback([this](const ix::WebSocketMessagePtr &msg) {
            if (!msg)
                return;
            if (msg->type == ix::WebSocketMessageType::Open)
            {
                impl_->open = true;
                impl_->attempt = 0;
                impl_->start_heart();
                impl_->replay();
                if (impl_->on_open)
                    impl_->on_open();
            }
            else if (msg->type == ix::WebSocketMessageType::Message)
            {
                WsMessage parsed;
                parsed.raw = msg->str;
                try
                {
                    const auto body = nlohmann::json::parse(msg->str);
                    if (body.contains("msgType") && body["msgType"].is_string())
                        parsed.msg_type = body["msgType"].get<std::string>();
                }
                catch (...)
                {
                }
                if (impl_->on_message)
                    impl_->on_message(parsed);
            }
            else if (msg->type == ix::WebSocketMessageType::Error)
            {
                if (impl_->on_error)
                    impl_->on_error(msg->errorInfo.reason.empty() ? "websocket error" : msg->errorInfo.reason);
            }
            else if (msg->type == ix::WebSocketMessageType::Close)
            {
                impl_->open = false;
                if (impl_->reconnect && !impl_->stopping)
                    impl_->schedule_reconnect();
            }
        });
    }

    WebSocketClient::WebSocketClient(const Environment &environment, std::string api_key)
        : WebSocketClient(environment.websocket_url, std::move(api_key))
    {
    }

    WebSocketClient::~WebSocketClient()
    {
        if (impl_)
        {
            impl_->stop();
            delete impl_;
            impl_ = nullptr;
        }
    }

    void WebSocketClient::set_message_callback(MessageCallback callback) { impl_->on_message = std::move(callback); }
    void WebSocketClient::set_error_callback(ErrorCallback callback) { impl_->on_error = std::move(callback); }
    void WebSocketClient::set_open_callback(VoidCallback callback) { impl_->on_open = std::move(callback); }

    void WebSocketClient::set_heartbeat_interval(std::chrono::seconds interval)
    {
        if (interval.count() <= 0)
            throw std::invalid_argument("heartbeat interval must be positive");
        impl_->heartbeat = interval;
    }

    void WebSocketClient::set_reconnect(bool enabled) { impl_->reconnect = enabled; }

    void WebSocketClient::connect()
    {
        impl_->stopping = false;
        impl_->socket.setUrl(impl_->url);
        impl_->socket.start();
    }

    void WebSocketClient::close() { impl_->stop(); }

    bool WebSocketClient::connected() const { return impl_->open; }

    void WebSocketClient::subscribe(const WsSubscription &subscription)
    {
        {
            std::lock_guard lock(impl_->mu);
            impl_->subscriptions.push_back(subscription);
        }
        if (impl_->open)
            impl_->socket.send(build_channel_message("SUBSCRIBE", subscription));
    }

    void WebSocketClient::unsubscribe(const WsSubscription &subscription)
    {
        {
            std::lock_guard lock(impl_->mu);
            std::erase_if(impl_->subscriptions, [&](const WsSubscription &existing) {
                return existing.channel == subscription.channel && existing.market_id == subscription.market_id &&
                       existing.root_market_id == subscription.root_market_id;
            });
        }
        if (impl_->open)
            impl_->socket.send(build_channel_message("UNSUBSCRIBE", subscription));
    }

    MarketStream::MarketStream(const Environment &environment, std::string api_key)
        : socket_(environment, std::move(api_key))
    {
    }

    MarketStream::~MarketStream() = default;

    void MarketStream::connect() { socket_.connect(); }
    void MarketStream::close() { socket_.close(); }

    void MarketStream::subscribe_depth(std::int64_t market_id)
    {
        socket_.subscribe(WsSubscription{std::string(channel::kDepthDiff), market_id, std::nullopt});
    }

    void MarketStream::subscribe_last_price(std::int64_t market_id)
    {
        socket_.subscribe(WsSubscription{std::string(channel::kLastPrice), market_id, std::nullopt});
    }

    void MarketStream::subscribe_last_price_root(std::int64_t root_market_id)
    {
        socket_.subscribe(WsSubscription{std::string(channel::kLastPrice), std::nullopt, root_market_id});
    }

    void MarketStream::subscribe_last_trade(std::int64_t market_id)
    {
        socket_.subscribe(WsSubscription{std::string(channel::kLastTrade), market_id, std::nullopt});
    }

    void MarketStream::subscribe_last_trade_root(std::int64_t root_market_id)
    {
        socket_.subscribe(WsSubscription{std::string(channel::kLastTrade), std::nullopt, root_market_id});
    }
} // namespace opinion
