#include "opinion/user_stream.hpp"

namespace opinion
{
    UserStream::UserStream(const Environment &environment, std::string api_key)
        : socket_(environment, std::move(api_key))
    {
    }

    UserStream::~UserStream() = default;

    void UserStream::connect() { socket_.connect(); }
    void UserStream::close() { socket_.close(); }

    void UserStream::subscribe_order_update(std::int64_t market_id)
    {
        socket_.subscribe(WsSubscription{std::string(channel::kOrderUpdate), market_id, std::nullopt});
    }

    void UserStream::subscribe_order_update_root(std::int64_t root_market_id)
    {
        socket_.subscribe(WsSubscription{std::string(channel::kOrderUpdate), std::nullopt, root_market_id});
    }

    void UserStream::subscribe_trade_record(std::int64_t market_id)
    {
        socket_.subscribe(WsSubscription{std::string(channel::kTradeRecord), market_id, std::nullopt});
    }

    void UserStream::subscribe_trade_record_root(std::int64_t root_market_id)
    {
        socket_.subscribe(WsSubscription{std::string(channel::kTradeRecord), std::nullopt, root_market_id});
    }
} // namespace opinion
