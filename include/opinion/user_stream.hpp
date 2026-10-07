#pragma once

#include "opinion/types.hpp"
#include "opinion/websocket_client.hpp"

#include <functional>
#include <string>

namespace opinion
{
    struct OrderUpdateEvent
    {
        std::string order_update_type;
        std::int64_t market_id{0};
        std::int64_t root_market_id{0};
        std::string order_id;
        int side{0};
        int outcome_side{0};
        std::string price;
        std::string shares;
        std::string amount;
        int status{0};
        int trading_method{0};
        std::string quote_token;
        std::int64_t created_at{0};
        std::int64_t expires_at{0};
        std::string chain_id;
        std::string filled_shares;
        std::string filled_amount;
        std::string raw;
    };

    struct TradeRecordEvent
    {
        std::string order_id;
        std::string trade_no;
        std::string tx_hash;
        std::int64_t market_id{0};
        std::int64_t root_market_id{0};
        std::string side;
        int outcome_side{0};
        std::string price;
        std::string shares;
        std::string amount;
        std::string profit;
        int status{0};
        std::string quote_token;
        std::string fee;
        std::string chain_id;
        std::int64_t created_at{0};
        std::string raw;
    };

    // User-channel skeleton. A matched trade on market.last.trade is not an
    // on-chain fill; trade.record.new is the confirmed fill channel.
    class UserStream
    {
    public:
        using OrderCallback = std::function<void(const OrderUpdateEvent &)>;
        using TradeCallback = std::function<void(const TradeRecordEvent &)>;

        explicit UserStream(WebSocketClient &socket);

        void on_order_update(OrderCallback callback);
        void on_trade(TradeCallback callback);

        bool subscribe_orders(std::int64_t market_id, bool categorical_root = false);
        bool subscribe_trades(std::int64_t market_id, bool categorical_root = false);
        void handle_message(const std::string &message);

    private:
        WebSocketClient *socket_;
        OrderCallback on_order_;
        TradeCallback on_trade_;
    };
} // namespace opinion
