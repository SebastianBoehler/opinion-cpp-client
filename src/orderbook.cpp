#include "opinion/orderbook.hpp"

namespace opinion
{
    namespace
    {
        bool zero_size(const std::string &size)
        {
            if (size.empty() || size == "0")
                return true;
            bool saw_digit = false;
            for (char c : size)
            {
                if (c == '.')
                    continue;
                if (c < '0' || c > '9')
                    return false;
                if (c != '0')
                    saw_digit = true;
            }
            return !saw_digit;
        }

        void apply(std::map<std::string, std::string> &levels, const std::string &price, const std::string &size)
        {
            if (zero_size(size))
                levels.erase(price);
            else
                levels[price] = size;
        }
    } // namespace

    void LocalOrderBook::reset(const OrderBook &snapshot)
    {
        token_id_ = snapshot.token_id;
        bids_.clear();
        asks_.clear();
        for (const auto &level : snapshot.bids)
            apply(bids_, level.price, level.size);
        for (const auto &level : snapshot.asks)
            apply(asks_, level.price, level.size);
    }

    void LocalOrderBook::apply_level(const std::string &side, const std::string &price, const std::string &size)
    {
        if (side == "bids")
            apply(bids_, price, size);
        else if (side == "asks")
            apply(asks_, price, size);
    }
} // namespace opinion
