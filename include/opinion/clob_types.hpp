#pragma once

#include "opinion/order_signer.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace opinion
{
    struct MarketQuery
    {
        std::optional<int> page;
        std::optional<int> limit;
        // Docs: "activated" or "resolved".
        std::optional<std::string> status;
        // Docs embedded OpenAPI: 0 = binary, 1 = categorical, 2 = all.
        std::optional<int> market_type;
        // 1=new, 2=ending soon, 3=volume desc, 4=volume asc,
        // 5=volume24h desc, 6=volume24h asc, 7=volume7d desc, 8=volume7d asc.
        std::optional<int> sort_by;
        std::optional<std::string> chain_id;
        std::optional<std::int64_t> label_id;
    };

    struct ChildMarket
    {
        std::int64_t market_id{0};
        std::string market_title;
        int status{0};
        std::string status_enum;
        std::string yes_label;
        std::string no_label;
        std::string yes_token_id;
        std::string no_token_id;
        std::string condition_id;
        std::string quote_token;
        std::string chain_id;
        std::string volume;
    };

    struct Market
    {
        std::int64_t market_id{0};
        std::string market_title;
        int status{0};
        std::string status_enum;
        int market_type{0};
        std::string yes_label;
        std::string no_label;
        std::string rules;
        std::string yes_token_id;
        std::string no_token_id;
        std::string condition_id;
        std::string result_token_id;
        std::string volume;
        std::string volume_24h;
        std::string volume_7d;
        std::string quote_token;
        std::string chain_id;
        std::string question_id;
        std::string slug;
        std::int64_t created_at{0};
        std::int64_t cutoff_at{0};
        std::int64_t resolved_at{0};
        std::vector<std::string> labels;
        std::vector<std::int64_t> label_ids;
        std::vector<ChildMarket> child_markets;
    };

    struct MarketList
    {
        std::int64_t total{0};
        std::vector<Market> list;
    };

    struct Label
    {
        std::int64_t label_id{0};
        std::string label_name;
        std::string image_url;
        std::string image_mobile_url;
    };

    struct BookLevel
    {
        std::string price;
        std::string size;
    };

    struct OrderBook
    {
        std::string market;
        std::string token_id;
        std::int64_t timestamp_ms{0};
        std::vector<BookLevel> bids;
        std::vector<BookLevel> asks;
    };

    struct LatestPrice
    {
        std::string token_id;
        std::string price;
        std::string side;
        std::string size;
        std::int64_t timestamp_ms{0};
    };

    struct PricePoint
    {
        std::int64_t t{0};
        std::string p;
    };

    struct PriceHistory
    {
        std::vector<PricePoint> history;
    };

    struct PriceHistoryQuery
    {
        std::string token_id;
        // Docs: 1m, 1h, 1d, 1w, max. Default on the server is 1d.
        std::optional<std::string> interval;
        std::optional<std::int64_t> start_at;
        std::optional<std::int64_t> end_at;
    };

    struct QuoteTokenQuery
    {
        std::optional<int> page;
        std::optional<int> limit;
        std::optional<std::string> quote_token_name;
        std::optional<std::string> chain_id;
    };

    struct QuoteToken
    {
        std::int64_t id{0};
        std::string quote_token_name;
        std::string quote_token_address;
        std::string ctf_exchange_address;
        int decimal{0};
        std::string symbol;
        std::string chain_id;
        std::int64_t created_at{0};
    };

    struct ApiKeyCredential
    {
        std::string api_key;
        std::string wallet_address;
    };

    struct OrderQuery
    {
        std::optional<int> page;
        std::optional<int> limit;
        std::optional<std::int64_t> market_id;
        std::optional<std::string> chain_id;
        // Numeric status or comma-separated, e.g. "1,2".
        // 1=pending, 2=filled, 3=canceled, 4=expired, 5=failed.
        std::optional<std::string> status;
    };

    struct OrderRecord
    {
        std::string order_id;
        int status{0};
        std::string status_enum;
        std::int64_t market_id{0};
        std::string market_title;
        int side{0};
        std::string side_enum;
        int trading_method{0};
        std::string trading_method_enum;
        std::string outcome;
        std::string price;
        std::string order_shares;
        std::string order_amount;
        std::string filled_shares;
        std::string filled_amount;
        std::string quote_token;
        std::int64_t created_at{0};
        std::optional<bool> post_only;
        std::string raw_json;
    };

    struct OrderList
    {
        std::int64_t total{0};
        std::vector<OrderRecord> list;
    };

    struct PositionQuery
    {
        std::optional<int> page;
        std::optional<int> limit;
        std::optional<std::int64_t> market_id;
        std::optional<std::string> chain_id;
    };

    struct Position
    {
        std::int64_t market_id{0};
        std::string market_title;
        int market_status{0};
        std::string outcome;
        int outcome_side{0};
        std::string shares_owned;
        std::string token_id;
        std::string avg_entry_price;
        std::string quote_token;
        std::string raw_json;
    };

    struct PositionList
    {
        std::int64_t total{0};
        std::vector<Position> list;
    };

    // Human-readable amounts. Set exactly one of the maker amount fields.
    // Price is required for limit orders (0.01–0.99, at most 4 decimal places).
    // maker is the Safe / multiSig. The OrderSigner address is the EIP-712 signer.
    struct PlaceOrderRequest
    {
        std::int64_t market_id{0};
        std::string token_id;
        OrderSide side{OrderSide::Buy};
        OrderType order_type{OrderType::Limit};
        std::string price;
        std::optional<std::string> maker_amount_in_quote_token;
        std::optional<std::string> maker_amount_in_base_token;
        std::string maker;
        // Serialized only when set. Documented on the TypeScript order page and
        // on OpenAPI OrderData. opinion_api 0.4.0 V2AddOrderReq does not list it.
        std::optional<bool> post_only;
        // On-chain enableTrading is not part of this v1 client.
        bool check_approval{false};
    };

    struct PlaceOrderResult
    {
        OrderRecord order;
        SignedOrder signed_order;
        std::string raw_result_json;
    };

    struct CancelOrderResult
    {
        std::string raw_result_json;
    };
} // namespace opinion
