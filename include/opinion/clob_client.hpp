#pragma once

#include "opinion/clob_types.hpp"
#include "opinion/environment.hpp"
#include "opinion/http_client.hpp"
#include "opinion/sdk_error.hpp"

#include <memory>
#include <string>
#include <string_view>

namespace opinion
{
    // REST facade for the Opinion OpenAPI host.
    //
    // Public reads (no apikey): GET /market, /market/{id},
    // /market/categorical/{id}, /market/slug/{slug}, /label,
    // /token/orderbook, /token/latest-price, /token/price-history, /quoteToken.
    //
    // API key header `apikey` is attached only when set. An invalid key is a
    // hard 401 on authenticated routes; there is no anonymous fallback.
    //
    // Mutations:
    //   POST {rest_base}/order
    //   POST {rest_base}/order/cancel   body {"orderId":"..."}
    // rest_base already contains `/openapi` (see Environment).
    class ClobClient
    {
    public:
        explicit ClobClient(Environment environment);
        ClobClient(Environment environment, std::string api_key);
        ~ClobClient();

        ClobClient(const ClobClient &) = delete;
        ClobClient &operator=(const ClobClient &) = delete;
        ClobClient(ClobClient &&) noexcept;
        ClobClient &operator=(ClobClient &&) noexcept;

        const Environment &environment() const { return environment_; }
        void set_api_key(std::string api_key);
        const std::string &api_key() const { return api_key_; }
        void set_signer(std::unique_ptr<OrderSigner> signer);

        HttpClient &http() { return http_; }

        Result<MarketList> list_markets(const MarketQuery &query = {});
        Result<Market> get_market(std::int64_t market_id);
        Result<Market> get_categorical_market(std::int64_t market_id);
        Result<Market> get_market_by_slug(std::string_view slug);
        Result<std::vector<Label>> list_labels();

        Result<OrderBook> get_orderbook(std::string_view token_id);
        Result<LatestPrice> get_latest_price(std::string_view token_id);
        Result<PriceHistory> get_price_history(const PriceHistoryQuery &query);
        Result<std::vector<QuoteToken>> list_quote_tokens(const QuoteTokenQuery &query = {});

        // EIP-712 wallet signature. Requires set_signer. No apikey header.
        // Signatures expire after 5 minutes. create and delete are single-use.
        // A new key can take about 15 seconds to pass the gateway.
        Result<ApiKeyCredential> create_api_key(std::string timestamp = {});
        Result<ApiKeyCredential> get_api_key(std::string timestamp = {});
        Result<bool> delete_api_key(std::string timestamp = {});

        Result<OrderList> list_orders(const OrderQuery &query = {});
        Result<OrderRecord> get_order(std::string_view order_id);
        Result<PlaceOrderResult> place_order(const PlaceOrderRequest &request);
        Result<CancelOrderResult> cancel_order(std::string_view order_id);

        // Docs: GET /positions/user/{walletAddress}. Requires apikey.
        // The older generated opinion_api 0.4.0 client used GET /openapi/positions
        // instead; this client follows the current position page.
        Result<PositionList> list_positions(std::string_view wallet, const PositionQuery &query = {});

    private:
        Environment environment_;
        HttpClient http_;
        std::string api_key_;
        std::unique_ptr<OrderSigner> signer_;

        void apply_api_key_header();
    };
} // namespace opinion
