#pragma once

#include "opinion/types.hpp"
#include "opinion/websocket_client.hpp"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace opinion
{
    // Local book. A REST snapshot replaces both sides. A market.depth.diff
    // message updates one price level. A size that parses as zero removes the
    // level; the channel docs show the fields but do not spell out the zero-size
    // convention.
    class LocalOrderbook
    {
    public:
        void apply_snapshot(const Orderbook &snapshot);
        // Returns false when the payload is not a depth diff for this book.
        bool apply_depth_message(const std::string &json_message);

        const Orderbook &book() const { return book_; }
        std::optional<OrderbookLevel> best_bid() const;
        std::optional<OrderbookLevel> best_ask() const;

    private:
        Orderbook book_;
    };

    class OrderbookStream
    {
    public:
        using UpdateCallback = std::function<void(const Orderbook &)>;

        explicit OrderbookStream(WebSocketClient &socket);

        void on_update(UpdateCallback callback);
        bool subscribe(std::int64_t market_id);
        void handle_message(const std::string &message);
        const LocalOrderbook &book() const { return book_; }

    private:
        WebSocketClient *socket_;
        LocalOrderbook book_;
        UpdateCallback callback_;
    };
} // namespace opinion
