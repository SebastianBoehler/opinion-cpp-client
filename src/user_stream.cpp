#include "opinion/user_stream.hpp"

#include <nlohmann/json.hpp>

namespace opinion
{
    namespace
    {
        std::int64_t number_or_zero(const nlohmann::json &object, const char *key)
        {
            if (!object.contains(key) || object[key].is_null())
            {
                return 0;
            }
            if (object[key].is_number_integer())
            {
                return object[key].get<std::int64_t>();
            }
            if (object[key].is_string())
            {
                try
                {
                    return std::stoll(object[key].get<std::string>());
                }
                catch (const std::exception &)
                {
                    return 0;
                }
            }
            return 0;
        }

        std::string text_or_empty(const nlohmann::json &object, const char *key)
        {
            if (!object.contains(key) || object[key].is_null())
            {
                return {};
            }
            if (object[key].is_string())
            {
                return object[key].get<std::string>();
            }
            if (object[key].is_number())
            {
                return object[key].dump();
            }
            return {};
        }
    } // namespace

    UserStream::UserStream(WebSocketClient &socket) : socket_(&socket) {}

    void UserStream::on_order_update(OrderCallback callback) { on_order_ = std::move(callback); }

    void UserStream::on_trade(TradeCallback callback) { on_trade_ = std::move(callback); }

    bool UserStream::subscribe_orders(std::int64_t market_id, bool categorical_root)
    {
        return socket_->subscribe(WebSocketClient::subscribe_message(k_channel_order_update, market_id, categorical_root));
    }

    bool UserStream::subscribe_trades(std::int64_t market_id, bool categorical_root)
    {
        return socket_->subscribe(WebSocketClient::subscribe_message(k_channel_trade_record, market_id, categorical_root));
    }

    void UserStream::handle_message(const std::string &message)
    {
        const auto document = nlohmann::json::parse(message, nullptr, false);
        if (!document.is_object())
        {
            return;
        }
        const std::string type = document.value("msgType", "");
        if (type == k_channel_order_update && on_order_)
        {
            OrderUpdateEvent event;
            event.order_update_type = text_or_empty(document, "orderUpdateType");
            event.market_id = number_or_zero(document, "marketId");
            event.root_market_id = number_or_zero(document, "rootMarketId");
            event.order_id = text_or_empty(document, "orderId");
            event.side = static_cast<int>(number_or_zero(document, "side"));
            event.outcome_side = static_cast<int>(number_or_zero(document, "outcomeSide"));
            event.price = text_or_empty(document, "price");
            event.shares = text_or_empty(document, "shares");
            event.amount = text_or_empty(document, "amount");
            event.status = static_cast<int>(number_or_zero(document, "status"));
            event.trading_method = static_cast<int>(number_or_zero(document, "tradingMethod"));
            event.quote_token = text_or_empty(document, "quoteToken");
            event.created_at = number_or_zero(document, "createdAt");
            event.expires_at = number_or_zero(document, "expiresAt");
            event.chain_id = text_or_empty(document, "chainId");
            event.filled_shares = text_or_empty(document, "filledShares");
            event.filled_amount = text_or_empty(document, "filledAmount");
            event.raw = message;
            on_order_(event);
        }
        else if (type == k_channel_trade_record && on_trade_)
        {
            TradeRecordEvent event;
            event.order_id = text_or_empty(document, "orderId");
            event.trade_no = text_or_empty(document, "tradeNo");
            event.tx_hash = text_or_empty(document, "txHash");
            event.market_id = number_or_zero(document, "marketId");
            event.root_market_id = number_or_zero(document, "rootMarketId");
            event.side = text_or_empty(document, "side");
            event.outcome_side = static_cast<int>(number_or_zero(document, "outcomeSide"));
            event.price = text_or_empty(document, "price");
            event.shares = text_or_empty(document, "shares");
            event.amount = text_or_empty(document, "amount");
            event.profit = text_or_empty(document, "profit");
            event.status = static_cast<int>(number_or_zero(document, "status"));
            event.quote_token = text_or_empty(document, "quoteToken");
            event.fee = text_or_empty(document, "fee");
            event.chain_id = text_or_empty(document, "chainId");
            event.created_at = number_or_zero(document, "createdAt");
            event.raw = message;
            on_trade_(event);
        }
    }
} // namespace opinion
