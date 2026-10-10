#pragma once

#include "opinion/environment.hpp"
#include "opinion/http_client.hpp"
#include "opinion/order_signer.hpp"
#include "opinion/sdk_error.hpp"
#include "opinion/types.hpp"
#include <nlohmann/json_fwd.hpp>

#include <map>
#include <string>

namespace opinion
{
    struct ClientConfig
    {
        std::string api_key;
        HttpClientOptions http;
    };

    // REST facade for the Opinion OpenAPI.
    // Logical paths are resolved through Environment so the openapi host and the
    // proxy host share one call site.
    class ClobClient
    {
    public:
        explicit ClobClient(Environment environment = Environment::bnb_mainnet(),
                            ClientConfig config = {});

        const Environment &environment() const { return environment_; }
        void set_api_key(std::string api_key);
        const std::string &api_key() const { return api_key_; }

        Result<MarketList> list_markets(const MarketListQuery &query = {}) const;
        Result<Market> get_market(std::int64_t market_id) const;
        Result<Market> get_categorical_market(std::int64_t market_id) const;
        Result<Market> get_market_by_slug(const std::string &slug) const;
        Result<std::vector<Label>> list_labels() const;

        Result<Orderbook> get_orderbook(const std::string &token_id) const;
        Result<LatestPrice> get_latest_price(const std::string &token_id) const;
        Result<std::vector<PricePoint>> get_price_history(const PriceHistoryQuery &query) const;
        Result<QuoteTokenList> list_quote_tokens(const QuoteTokenQuery &query = {}) const;
        Result<FeeRates> get_fee_rates(const std::string &token_id, const std::string &chain_id = "56") const;

        Result<ApiKeyCredential> create_api_key(const OrderSigner &signer) const;
        Result<ApiKeyCredential> get_api_key(const OrderSigner &signer) const;
        Result<bool> delete_api_key(const OrderSigner &signer) const;
        Result<ApiKeyCredential> get_user_auth() const;

        Result<OrderList> list_orders(const PageQuery &query = {}) const;
        Result<OrderRecord> get_order(const std::string &order_id) const;
        Result<PositionList> list_positions(const PageQuery &query = {}) const;
        Result<PositionList> list_positions(const std::string &wallet_address, const PageQuery &query = {}) const;
        Result<TradeList> list_trades(const PageQuery &query = {}) const;
        Result<TradeList> list_trades(const std::string &wallet_address, const PageQuery &query = {}) const;
        Result<Balance> get_balance(const std::string &chain_id = "56") const;

        // Builds the V2AddOrderReq JSON and signs the CTF order. Does not post.
        Result<PreparedOrder> prepare_order(const OrderSigner &signer, const PlaceOrderRequest &request) const;
        // POST /order. Resolves exchange address and decimals from /quoteToken when
        // request.exchange_address is empty, using the market's quoteToken.
        Result<MutationResult> place_order(const OrderSigner &signer, PlaceOrderRequest request) const;
        // POST /order/cancel with {"orderId": "..."}.
        Result<MutationResult> cancel_order(const std::string &order_id) const;

    private:
        Environment environment_;
        std::string api_key_;
        mutable HttpClient http_;
        mutable HttpClient order_http_;

        Result<ApiKeyCredential> exchange_api_key(const OrderSigner &signer,
                                                  ApiKeyAction action,
                                                  const std::string &method) const;
        Result<nlohmann::json> call(const std::string &method, const std::string &logical_path,
                                    const std::map<std::string, std::string> &query, const std::string &body,
                                    bool authenticated,
                                    const std::map<std::string, std::string> &extra_headers = {}) const;
    };
} // namespace opinion
