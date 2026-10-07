#pragma once

#include "opinion/websocket_client.hpp"

#include <string>

namespace opinion
{
    // Authenticated user channels on the same WebSocket as market data.
    // trade.order.update is a match/cancel/confirm notice and is not proof of
    // the on-chain fill. trade.record.new is the confirmed trade (or split/merge).
    class UserStream
    {
    public:
        UserStream(const Environment &environment, std::string api_key);
        ~UserStream();

        UserStream(const UserStream &) = delete;
        UserStream &operator=(const UserStream &) = delete;

        WebSocketClient &socket() { return socket_; }

        void connect();
        void close();

        void subscribe_order_update(std::int64_t market_id);
        void subscribe_order_update_root(std::int64_t root_market_id);
        void subscribe_trade_record(std::int64_t market_id);
        void subscribe_trade_record_root(std::int64_t root_market_id);

    private:
        WebSocketClient socket_;
    };
} // namespace opinion
