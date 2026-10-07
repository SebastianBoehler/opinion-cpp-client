#include "opinion/websocket_client.hpp"

#include "opinion/network.hpp"

#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocket.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace opinion
{
    namespace
    {
        void ensure_net_system()
        {
            static std::once_flag once;
            std::call_once(once, [] { ix::initNetSystem(); });
        }

        std::string channel_message(std::string_view action,
                                    std::string_view channel,
                                    std::int64_t market_id,
                                    bool categorical_root)
        {
            std::string message = std::string("{\"action\":\"") + std::string(action) + "\",\"channel\":\"" +
                                  std::string(channel) + "\",";
            message += categorical_root ? "\"rootMarketId\":" : "\"marketId\":";
            message += std::to_string(market_id);
            message += "}";
            return message;
        }
    } // namespace

    struct WebSocketClient::Impl
    {
        ix::WebSocket socket;
        WebSocketOptions options;
        std::string url;
        mutable std::mutex mutex;
        MessageCallback on_message;
        VoidCallback on_open;
        VoidCallback on_close;
        ErrorCallback on_error;
        std::vector<std::string> subscriptions;
        std::atomic<WsState> state{WsState::Disconnected};
        std::atomic<bool> heartbeat_running{false};
        std::thread heartbeat_thread;

        Impl() { ensure_net_system(); }

        ~Impl()
        {
            stop_heartbeat();
            socket.stop();
        }

        void stop_heartbeat()
        {
            heartbeat_running = false;
            if (heartbeat_thread.joinable())
            {
                heartbeat_thread.join();
            }
        }

        void start_heartbeat()
        {
            stop_heartbeat();
            if (options.heartbeat_interval_seconds <= 0)
            {
                return;
            }
            heartbeat_running = true;
            heartbeat_thread = std::thread([this] {
                while (heartbeat_running)
                {
                    for (int i = 0; i < options.heartbeat_interval_seconds * 10 && heartbeat_running; ++i)
                    {
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    }
                    if (!heartbeat_running)
                    {
                        break;
                    }
                    if (state == WsState::Connected)
                    {
                        socket.sendText(R"({"action":"HEARTBEAT"})");
                    }
                }
            });
        }
    };

    WebSocketClient::WebSocketClient() : impl_(std::make_unique<Impl>()) {}
    WebSocketClient::~WebSocketClient() = default;

    void WebSocketClient::configure(const WebSocketOptions &options)
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->options = options;
    }

    void WebSocketClient::set_url(std::string url)
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->url = std::move(url);
    }

    void WebSocketClient::on_message(MessageCallback callback)
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->on_message = std::move(callback);
    }

    void WebSocketClient::on_open(VoidCallback callback)
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->on_open = std::move(callback);
    }

    void WebSocketClient::on_close(VoidCallback callback)
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->on_close = std::move(callback);
    }

    void WebSocketClient::on_error(ErrorCallback callback)
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->on_error = std::move(callback);
    }

    bool WebSocketClient::connect()
    {
        std::string url;
        WebSocketOptions options;
        {
            std::lock_guard<std::mutex> lock(impl_->mutex);
            url = impl_->url;
            options = impl_->options;
        }
        if (url.empty())
        {
            return false;
        }
        const NetworkRoute route = default_network_route();
        if (options.proxy_url.empty())
        {
            options.proxy_url = route.proxy_url;
        }
        impl_->socket.setUrl(url);
        impl_->socket.setPingInterval(options.ping_interval_seconds);
        if (options.auto_reconnect)
        {
            impl_->socket.enableAutomaticReconnection();
        }
        else
        {
            impl_->socket.disableAutomaticReconnection();
        }
        impl_->socket.setOnMessageCallback([this](const ix::WebSocketMessagePtr &message) {
            if (message->type == ix::WebSocketMessageType::Open)
            {
                impl_->state = WsState::Connected;
                std::vector<std::string> replay;
                VoidCallback opened;
                {
                    std::lock_guard<std::mutex> lock(impl_->mutex);
                    replay = impl_->subscriptions;
                    opened = impl_->on_open;
                }
                for (const auto &subscription : replay)
                {
                    impl_->socket.sendText(subscription);
                }
                if (opened)
                {
                    opened();
                }
            }
            else if (message->type == ix::WebSocketMessageType::Close)
            {
                impl_->state = WsState::Disconnected;
                VoidCallback closed;
                {
                    std::lock_guard<std::mutex> lock(impl_->mutex);
                    closed = impl_->on_close;
                }
                if (closed)
                {
                    closed();
                }
            }
            else if (message->type == ix::WebSocketMessageType::Error)
            {
                ErrorCallback failed;
                {
                    std::lock_guard<std::mutex> lock(impl_->mutex);
                    failed = impl_->on_error;
                }
                if (failed)
                {
                    failed(message->errorInfo.reason);
                }
            }
            else if (message->type == ix::WebSocketMessageType::Message)
            {
                MessageCallback handler;
                {
                    std::lock_guard<std::mutex> lock(impl_->mutex);
                    handler = impl_->on_message;
                }
                if (handler)
                {
                    handler(message->str);
                }
            }
        });
        impl_->state = WsState::Connecting;
        impl_->socket.start();
        impl_->start_heartbeat();
        return true;
    }

    bool WebSocketClient::wait_until_connected(std::chrono::milliseconds timeout)
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (impl_->state == WsState::Connected)
            {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        return impl_->state == WsState::Connected;
    }

    void WebSocketClient::disconnect()
    {
        impl_->state = WsState::Closing;
        impl_->stop_heartbeat();
        impl_->socket.stop();
        impl_->state = WsState::Disconnected;
    }

    bool WebSocketClient::is_connected() const { return impl_->state == WsState::Connected; }

    WsState WebSocketClient::state() const { return impl_->state; }

    bool WebSocketClient::send(const std::string &message)
    {
        if (impl_->state != WsState::Connected)
        {
            return false;
        }
        return impl_->socket.sendText(message).success;
    }

    bool WebSocketClient::send_heartbeat() { return send(R"({"action":"HEARTBEAT"})"); }

    bool WebSocketClient::subscribe(const std::string &message)
    {
        {
            std::lock_guard<std::mutex> lock(impl_->mutex);
            impl_->subscriptions.push_back(message);
        }
        if (impl_->state == WsState::Connected)
        {
            return impl_->socket.sendText(message).success;
        }
        return true;
    }

    bool WebSocketClient::unsubscribe(const std::string &message)
    {
        {
            std::lock_guard<std::mutex> lock(impl_->mutex);
            auto &items = impl_->subscriptions;
            items.erase(std::remove(items.begin(), items.end(), message), items.end());
        }
        if (impl_->state == WsState::Connected)
        {
            return impl_->socket.sendText(message).success;
        }
        return true;
    }

    std::string WebSocketClient::subscribe_message(std::string_view channel, std::int64_t market_id, bool categorical_root)
    {
        return channel_message("SUBSCRIBE", channel, market_id, categorical_root);
    }

    std::string WebSocketClient::unsubscribe_message(std::string_view channel, std::int64_t market_id, bool categorical_root)
    {
        return channel_message("UNSUBSCRIBE", channel, market_id, categorical_root);
    }
} // namespace opinion
