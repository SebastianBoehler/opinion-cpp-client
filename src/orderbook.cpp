#include "opinion/orderbook.hpp"

#include "book_decimal.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace opinion
{
    namespace
    {
        using detail::compare_prices;
        bool before(const OrderbookLevel &left, const OrderbookLevel &right, bool bids)
        {
            const int result = compare_prices(left.price, right.price);
            return bids ? result > 0 : result < 0;
        }
        void sort_levels(std::vector<OrderbookLevel> &levels, bool bids)
        {
            for (const auto &level : levels)
            {
                detail::decimal_view(level.price);
                detail::decimal_view(level.size);
            }
            std::sort(levels.begin(), levels.end(),
                      [bids](const auto &left, const auto &right) { return before(left, right, bids); });
            for (std::size_t i = 1; i < levels.size(); ++i)
                if (compare_prices(levels[i - 1].price, levels[i].price) == 0)
                    throw std::invalid_argument("duplicate book price");
        }
        void upsert(std::vector<OrderbookLevel> &levels, const std::string &price, const std::string &size, bool bids)
        {
            const bool zero = detail::is_zero_size(size);
            const auto found = std::lower_bound(levels.begin(), levels.end(), price,
                                                [bids](const OrderbookLevel &level, const std::string &value)
                                                {
                                                    const int result = compare_prices(level.price, value);
                                                    return bids ? result > 0 : result < 0;
                                                });
            const bool exists = found != levels.end() && compare_prices(found->price, price) == 0;
            if (zero)
            {
                if (exists)
                    levels.erase(found);
            }
            else if (exists)
                found->size = size;
            else
                levels.insert(found, OrderbookLevel{price, size});
        }
    } // namespace

    void LocalOrderbook::apply_snapshot(const Orderbook &snapshot)
    {
        auto next = snapshot;
        sort_levels(next.bids, true);
        sort_levels(next.asks, false);
        book_ = std::move(next);
    }

    bool LocalOrderbook::apply_depth_message(const std::string &json_message)
    {
        const auto document = nlohmann::json::parse(json_message);
        const std::string type = document.value("msgType", "");
        if (type != k_channel_depth_diff)
        {
            return false;
        }
        const std::string token_id = document.value("tokenId", "");
        if (!book_.token_id.empty() && token_id != book_.token_id)
        {
            return false;
        }
        const std::string side = document.value("side", "");
        const std::string price = document.value("price", "");
        const std::string size = document.value("size", "");
        if (side != "bids" && side != "asks")
            return false;
        detail::decimal_view(price);
        detail::decimal_view(size);
        if (side == "bids")
        {
            upsert(book_.bids, price, size, true);
        }
        else
        {
            upsert(book_.asks, price, size, false);
        }
        if (book_.token_id.empty())
            book_.token_id = token_id;
        if (document.contains("marketId"))
            book_.timestamp_ms = 0;
        return true;
    }

    std::optional<OrderbookLevel> LocalOrderbook::best_bid() const
    {
        if (!book_.bids.empty())
        {
            return book_.bids.front();
        }
        return std::nullopt;
    }

    std::optional<OrderbookLevel> LocalOrderbook::best_ask() const
    {
        if (!book_.asks.empty())
        {
            return book_.asks.front();
        }
        return std::nullopt;
    }

    OrderbookStream::OrderbookStream(WebSocketClient &socket) : socket_(&socket) {}

    void OrderbookStream::on_update(UpdateCallback callback) { callback_ = std::move(callback); }

    bool OrderbookStream::subscribe(std::int64_t market_id)
    {
        return socket_->subscribe(WebSocketClient::subscribe_message(k_channel_depth_diff, market_id, false));
    }

    void OrderbookStream::handle_message(const std::string &message)
    {
        if (book_.apply_depth_message(message) && callback_)
        {
            callback_(book_.book());
        }
    }
} // namespace opinion
