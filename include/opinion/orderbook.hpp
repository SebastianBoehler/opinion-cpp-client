#pragma once

#include "opinion/clob_types.hpp"

#include <map>
#include <string>

namespace opinion
{
    // In-memory book. REST snapshots replace the book. A market.depth.diff
    // message is applied as the new absolute size at that price. A size of
    // "0" removes the level. The docs describe `size` as the shares at the
    // updated price, not as a signed delta.
    class LocalOrderBook
    {
    public:
        void reset(const OrderBook &snapshot);
        void apply_level(const std::string &side, const std::string &price, const std::string &size);

        const std::map<std::string, std::string> &bids() const { return bids_; }
        const std::map<std::string, std::string> &asks() const { return asks_; }
        const std::string &token_id() const { return token_id_; }

    private:
        std::string token_id_;
        // Keyed by the price string the venue sent. Not numerically sorted.
        std::map<std::string, std::string> bids_;
        std::map<std::string, std::string> asks_;
    };
} // namespace opinion
