#include "opinion/orderbook.hpp"

#include "opinion/decimal_math.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace opinion
{
    namespace
    {
        bool is_zero_size(const std::string &size)
        {
            try
            {
                const auto dot = size.find('.');
                const std::string whole = dot == std::string::npos ? size : size.substr(0, dot);
                const std::string fraction = dot == std::string::npos ? std::string() : size.substr(dot + 1);
                for (char character : whole + fraction)
                {
                    if (character != '0' && character != '+' && character != '.')
                    {
                        return false;
                    }
                }
                return true;
            }
            catch (const std::exception &)
            {
                return false;
            }
        }

        void upsert(std::vector<OrderbookLevel> &levels, const std::string &price, const std::string &size)
        {
            const auto found = std::find_if(levels.begin(), levels.end(), [&](const OrderbookLevel &level) {
                return level.price == price;
            });
            if (is_zero_size(size))
            {
                if (found != levels.end())
                {
                    levels.erase(found);
                }
                return;
            }
            if (found == levels.end())
            {
                levels.push_back(OrderbookLevel{price, size});
            }
            else
            {
                found->size = size;
            }
        }

        int compare_prices(std::string_view left, std::string_view right)
        {
            const auto split = [](std::string_view text)
            {
                const auto dot = text.find('.');
                std::string whole = dot == std::string_view::npos ? std::string(text) : std::string(text.substr(0, dot));
                std::string fraction = dot == std::string_view::npos ? std::string() : std::string(text.substr(dot + 1));
                if (whole.empty())
                {
                    whole = "0";
                }
                return std::make_pair(whole, fraction);
            };
            auto [left_whole, left_fraction] = split(left);
            auto [right_whole, right_fraction] = split(right);
            if (left_fraction.size() < right_fraction.size())
            {
                left_fraction.append(right_fraction.size() - left_fraction.size(), '0');
            }
            else if (right_fraction.size() < left_fraction.size())
            {
                right_fraction.append(left_fraction.size() - right_fraction.size(), '0');
            }
            return compare_decimal(left_whole + left_fraction, right_whole + right_fraction);
        }

        const OrderbookLevel *best_level(const std::vector<OrderbookLevel> &levels, bool highest)
        {
            const OrderbookLevel *best = nullptr;
            for (const auto &level : levels)
            {
                if (!best)
                {
                    best = &level;
                    continue;
                }
                try
                {
                    const int comparison = compare_prices(level.price, best->price);
                    if ((highest && comparison > 0) || (!highest && comparison < 0))
                    {
                        best = &level;
                    }
                }
                catch (const std::exception &)
                {
                }
            }
            return best;
        }
    } // namespace

    void LocalOrderbook::apply_snapshot(const Orderbook &snapshot) { book_ = snapshot; }

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
        if (book_.token_id.empty())
        {
            book_.token_id = token_id;
        }
        if (document.contains("marketId"))
        {
            book_.timestamp_ms = 0;
        }
        const std::string side = document.value("side", "");
        const std::string price = document.value("price", "");
        const std::string size = document.value("size", "");
        if (side == "bids")
        {
            upsert(book_.bids, price, size);
        }
        else if (side == "asks")
        {
            upsert(book_.asks, price, size);
        }
        else
        {
            return false;
        }
        return true;
    }

    std::optional<OrderbookLevel> LocalOrderbook::best_bid() const
    {
        if (const auto *level = best_level(book_.bids, true))
        {
            return *level;
        }
        return std::nullopt;
    }

    std::optional<OrderbookLevel> LocalOrderbook::best_ask() const
    {
        if (const auto *level = best_level(book_.asks, false))
        {
            return *level;
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
