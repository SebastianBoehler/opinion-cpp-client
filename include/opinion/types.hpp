#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace opinion
{
    // On-chain / EIP-712 order side. REST order records use a different
    // numbering (1 = buy, 2 = sell) and are stored as integers on OrderRecord.
    enum class OrderSide
    {
        Buy = 0,
        Sell = 1
    };

    enum class OrderType
    {
        Market = 1,
        Limit = 2
    };

    enum class SignatureType
    {
        Eoa = 0,
        PolyProxy = 1,
        PolyGnosisSafe = 2
    };

    enum class ApiKeyAction
    {
        Create,
        Get,
        Delete
    };

    struct PageQuery
    {
        std::optional<int> page;
        std::optional<int> limit;
        std::optional<std::int64_t> market_id;
        std::optional<std::string> chain_id;
        std::optional<std::string> status;
    };

    struct MarketListQuery
    {
        std::optional<int> page;
        std::optional<int> limit;
        // "activated" or "resolved".
        std::optional<std::string> status;
        // 0 = binary, 1 = categorical, 2 = all. Omitted leaves the server default.
        std::optional<int> market_type;
        // 1 = newest ... 8 = 7d volume ascending. See the market docs.
        std::optional<int> sort_by;
        std::optional<std::string> chain_id;
        std::optional<std::int64_t> label_id;
        // Generated client: 0 = active only, 1 = collection markets, 2 = exclude collections.
        std::optional<int> collection_type;
    };

    struct ResolutionSummary
    {
        std::string phase;
        std::string proposed_token_id;
        bool has_dispute_deadline{false};
        std::int64_t dispute_deadline{0};
    };

    struct Market
    {
        std::int64_t market_id{0};
        std::string market_title;
        std::string slug;
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
        std::int64_t created_at{0};
        std::int64_t cutoff_at{0};
        std::int64_t resolved_at{0};
        std::vector<std::string> labels;
        std::vector<std::int64_t> label_ids;
        std::optional<ResolutionSummary> resolution;
        std::vector<Market> child_markets;
    };

    struct MarketList
    {
        std::int64_t total{0};
        std::vector<Market> markets;
    };

    struct Label
    {
        std::int64_t label_id{0};
        std::string label_name;
        std::string image_url;
        std::string image_mobile_url;
    };

    struct OrderbookLevel
    {
        std::string price;
        std::string size;
    };

    struct Orderbook
    {
        std::string market;
        std::string token_id;
        std::int64_t timestamp_ms{0};
        std::vector<OrderbookLevel> bids;
        std::vector<OrderbookLevel> asks;
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
        std::int64_t timestamp_s{0};
        std::string price;
    };

    struct PriceHistoryQuery
    {
        std::string token_id;
        std::string interval{"1d"};
        std::optional<std::int64_t> start_at;
        std::optional<std::int64_t> end_at;
    };

    struct QuoteToken
    {
        std::int64_t id{0};
        std::string name;
        std::string address;
        std::string ctf_exchange_address;
        int decimals{0};
        std::string symbol;
        std::string chain_id;
        std::int64_t created_at{0};
    };

    struct QuoteTokenList
    {
        std::int64_t total{0};
        std::vector<QuoteToken> tokens;
    };

    struct QuoteTokenQuery
    {
        std::optional<int> page;
        std::optional<int> limit;
        std::optional<std::string> quote_token_name;
        std::optional<std::string> chain_id;
    };

    struct FeeRates
    {
        std::string token_id;
        std::string maker_fee_bps;
        std::string taker_fee_bps;
    };

    struct ApiKeyCredential
    {
        std::string api_key;
        std::string wallet_address;
    };

    struct OrderTrade
    {
        std::string order_no;
        std::string trade_no;
        std::string tx_hash;
        std::int64_t market_id{0};
        std::string market_title;
        std::int64_t root_market_id{0};
        std::string root_market_title;
        std::string side;
        std::string outcome;
        int outcome_side{0};
        std::string price;
        std::string shares;
        std::string amount;
        std::string fee;
        std::string fee_formatted;
        std::string profit;
        std::string quote_token;
        std::string quote_token_usd_price;
        std::string usd_amount;
        int status{0};
        std::string status_enum;
        std::string chain_id;
        std::int64_t created_at{0};
    };

    struct OrderRecord
    {
        std::string order_id;
        int status{0};
        std::string status_enum;
        std::int64_t market_id{0};
        std::string market_title;
        std::int64_t root_market_id{0};
        std::string root_market_title;
        int side{0};
        std::string side_enum;
        int trading_method{0};
        std::string trading_method_enum;
        int trading_unit{0};
        std::string outcome;
        int outcome_side{0};
        std::string price;
        std::string order_shares;
        std::string order_amount;
        std::string filled_shares;
        std::string filled_amount;
        std::string profit;
        std::string quote_token;
        std::int64_t created_at{0};
        std::int64_t expires_at{0};
        bool post_only{false};
        bool has_post_only{false};
        std::vector<OrderTrade> trades;
    };

    struct OrderList
    {
        std::int64_t total{0};
        std::vector<OrderRecord> orders;
    };

    struct Position
    {
        std::int64_t market_id{0};
        std::string market_title;
        int market_status{0};
        std::string market_status_enum;
        std::int64_t market_cutoff_at{0};
        std::int64_t root_market_id{0};
        std::string root_market_title;
        std::string outcome;
        int outcome_side{0};
        std::string shares_owned;
        std::string shares_frozen;
        std::string unrealized_pnl;
        std::string unrealized_pnl_percent;
        std::string daily_pnl_change;
        std::string daily_pnl_change_percent;
        std::string condition_id;
        std::string token_id;
        std::string current_value_in_quote_token;
        std::string avg_entry_price;
        int claim_status{0};
        std::string claim_status_enum;
        std::string quote_token;
    };

    struct PositionList
    {
        std::int64_t total{0};
        std::vector<Position> positions;
    };

    struct TradeList
    {
        std::int64_t total{0};
        std::vector<OrderTrade> trades;
    };

    struct TokenBalance
    {
        std::string quote_token;
        std::string available;
        std::string frozen;
        std::string total;
        int decimals{0};
    };

    struct Balance
    {
        std::string chain_id;
        std::string wallet_address;
        std::string multi_sign_address;
        std::vector<TokenBalance> balances;
    };

    // EIP-712 CTF order. Field order matches the official Order type.
    struct CtfOrder
    {
        std::string salt;
        std::string maker;
        std::string signer;
        std::string taker;
        std::string token_id;
        std::string maker_amount;
        std::string taker_amount;
        std::string expiration{"0"};
        std::string nonce{"0"};
        std::string fee_rate_bps{"0"};
        int side{0};
        int signature_type{0};
    };

    struct SignedCtfOrder
    {
        CtfOrder order;
        // keccak256(0x19 || 0x01 || domainSeparator || hashStruct(order)).
        // The official OrderBuilder names this value structHash and signs it
        // with raw secp256k1 (no personal_sign prefix).
        std::string digest;
        std::string signature;
    };

    struct PlaceOrderRequest
    {
        std::int64_t market_id{0};
        std::string token_id;
        OrderSide side{OrderSide::Buy};
        OrderType order_type{OrderType::Limit};
        std::string price;
        std::optional<std::string> maker_amount_in_quote_token;
        std::optional<std::string> maker_amount_in_base_token;
        bool post_only{false};
        // Safe / portfolio wallet. The official trading client always uses this as maker.
        std::string maker;
        // EOA that signs. Empty means the OrderSigner address.
        std::string signer;
        // From GET /quoteToken ctfExchangeAddress. Required to sign.
        std::string exchange_address;
        std::string currency_address;
        int decimals{18};
        std::string fee_rate_bps{"0"};
        std::string nonce{"0"};
        std::string expiration{"0"};
        std::optional<SignatureType> signature_type;
        // When set, used instead of a random salt. Tests pin this.
        std::optional<std::string> salt;
    };

    struct PreparedOrder
    {
        SignedCtfOrder signed_order;
        std::string body_json;
    };

    struct MutationResult
    {
        std::string raw_result;
        std::string order_id;
        bool accepted{false};
    };
} // namespace opinion
