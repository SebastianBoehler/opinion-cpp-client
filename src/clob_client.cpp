#include "opinion/clob_client.hpp"

#include "opinion/decimal_math.hpp"

#include <nlohmann/json.hpp>

#include <cctype>
#include <chrono>
#include <utility>

namespace opinion
{
    namespace
    {
        using json = nlohmann::json;

        std::string url_encode(std::string_view value)
        {
            static constexpr char kHex[] = "0123456789ABCDEF";
            std::string out;
            for (unsigned char c : value)
            {
                if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
                    out.push_back(static_cast<char>(c));
                else
                {
                    out.push_back('%');
                    out.push_back(kHex[c >> 4]);
                    out.push_back(kHex[c & 0x0f]);
                }
            }
            return out;
        }

        class Query
        {
        public:
            void add(std::string_view key, const std::string &value)
            {
                if (value.empty())
                    return;
                if (!raw_.empty())
                    raw_.push_back('&');
                raw_ += url_encode(key);
                raw_.push_back('=');
                raw_ += url_encode(value);
            }

            void add(std::string_view key, int value) { add(key, std::to_string(value)); }
            void add(std::string_view key, std::int64_t value) { add(key, std::to_string(value)); }

            std::string path(std::string base) const
            {
                if (raw_.empty())
                    return base;
                base.push_back('?');
                base += raw_;
                return base;
            }

        private:
            std::string raw_;
        };

        std::string excerpt(const std::string &body)
        {
            return body.substr(0, std::min<std::size_t>(body.size(), 512));
        }

        SdkError transport_error(const HttpResponse &response, const std::string &endpoint)
        {
            SdkError error;
            error.endpoint = endpoint;
            error.http_status = response.status_code;
            error.response_body_excerpt = excerpt(response.body);
            error.request_id = response.header("x-request-id");
            if (!response.error.empty())
            {
                error.code = SdkErrorCode::HttpTransport;
                error.message = response.error;
                error.retryable = true;
                return error;
            }
            if (response.status_code == 401)
            {
                error.code = SdkErrorCode::Auth;
                error.message = "unauthorized (invalid or missing apikey; no anonymous fallback)";
                return error;
            }
            if (response.status_code == 429)
            {
                error.code = SdkErrorCode::RateLimit;
                error.message = "rate limit exceeded";
                error.retryable = true;
                const std::string retry = response.header("retry-after");
                if (!retry.empty())
                    error.message += "; retry-after=" + retry;
                return error;
            }
            error.code = SdkErrorCode::ApiResponse;
            error.message = "HTTP " + std::to_string(response.status_code);
            error.retryable = response.status_code >= 500;
            return error;
        }

        struct Envelope
        {
            json result;
            std::string message;
        };

        Result<Envelope> parse_envelope(const HttpResponse &response, const std::string &endpoint)
        {
            if (!response.ok())
                return Result<Envelope>::failure(transport_error(response, endpoint));
            json body;
            try
            {
                body = json::parse(response.body);
            }
            catch (const std::exception &ex)
            {
                return Result<Envelope>::failure(make_parse_error(ex.what(), endpoint, response.body));
            }
            if (!body.is_object())
                return Result<Envelope>::failure(make_parse_error("response is not an object", endpoint, response.body));

            int business = 0;
            std::string message;
            bool have_code = false;
            if (body.contains("errno"))
            {
                have_code = true;
                business = body.value("errno", 0);
                message = body.value("errmsg", "");
            }
            else if (body.contains("code"))
            {
                have_code = true;
                business = body.value("code", 0);
                message = body.value("msg", "");
            }
            if (!have_code)
                return Result<Envelope>::failure(
                    make_parse_error("response has neither errno nor code", endpoint, response.body));
            if (business != 0)
            {
                SdkError error;
                error.code = SdkErrorCode::ApiResponse;
                error.business_code = business;
                error.message = message.empty() ? ("business code " + std::to_string(business)) : message;
                error.endpoint = endpoint;
                error.http_status = response.status_code;
                error.response_body_excerpt = excerpt(response.body);
                error.request_id = response.header("x-request-id");
                return Result<Envelope>::failure(std::move(error));
            }
            Envelope envelope;
            envelope.message = message;
            if (body.contains("result") && !body["result"].is_null())
                envelope.result = body["result"];
            else
                envelope.result = json::object();
            return Result<Envelope>::success(std::move(envelope));
        }

        std::string jstr(const json &object, const char *key)
        {
            if (!object.is_object() || !object.contains(key) || object[key].is_null())
                return {};
            const json &value = object[key];
            if (value.is_string())
                return value.get<std::string>();
            if (value.is_number_integer())
                return std::to_string(value.get<std::int64_t>());
            if (value.is_number())
                return value.dump();
            if (value.is_boolean())
                return value.get<bool>() ? "true" : "false";
            return {};
        }

        std::int64_t jint(const json &object, const char *key)
        {
            if (!object.is_object() || !object.contains(key) || object[key].is_null())
                return 0;
            const json &value = object[key];
            if (value.is_number_integer())
                return value.get<std::int64_t>();
            if (value.is_string())
            {
                try
                {
                    return std::stoll(value.get<std::string>());
                }
                catch (...)
                {
                    return 0;
                }
            }
            return 0;
        }

        int jint32(const json &object, const char *key)
        {
            return static_cast<int>(jint(object, key));
        }

        ChildMarket parse_child(const json &object)
        {
            ChildMarket child;
            child.market_id = jint(object, "marketId");
            child.market_title = jstr(object, "marketTitle");
            child.status = jint32(object, "status");
            child.status_enum = jstr(object, "statusEnum");
            child.yes_label = jstr(object, "yesLabel");
            child.no_label = jstr(object, "noLabel");
            child.yes_token_id = jstr(object, "yesTokenId");
            child.no_token_id = jstr(object, "noTokenId");
            child.condition_id = jstr(object, "conditionId");
            child.quote_token = jstr(object, "quoteToken");
            child.chain_id = jstr(object, "chainId");
            child.volume = jstr(object, "volume");
            return child;
        }

        Market parse_market(const json &object)
        {
            Market market;
            market.market_id = jint(object, "marketId");
            market.market_title = jstr(object, "marketTitle");
            market.status = jint32(object, "status");
            market.status_enum = jstr(object, "statusEnum");
            market.market_type = jint32(object, "marketType");
            market.yes_label = jstr(object, "yesLabel");
            market.no_label = jstr(object, "noLabel");
            market.rules = jstr(object, "rules");
            market.yes_token_id = jstr(object, "yesTokenId");
            market.no_token_id = jstr(object, "noTokenId");
            market.condition_id = jstr(object, "conditionId");
            market.result_token_id = jstr(object, "resultTokenId");
            market.volume = jstr(object, "volume");
            market.volume_24h = jstr(object, "volume24h");
            market.volume_7d = jstr(object, "volume7d");
            market.quote_token = jstr(object, "quoteToken");
            market.chain_id = jstr(object, "chainId");
            market.question_id = jstr(object, "questionId");
            market.slug = jstr(object, "slug");
            market.created_at = jint(object, "createdAt");
            market.cutoff_at = jint(object, "cutoffAt");
            market.resolved_at = jint(object, "resolvedAt");
            if (object.contains("labels") && object["labels"].is_array())
            {
                for (const auto &label : object["labels"])
                {
                    if (label.is_string())
                        market.labels.push_back(label.get<std::string>());
                }
            }
            if (object.contains("labelIds") && object["labelIds"].is_array())
            {
                for (const auto &id : object["labelIds"])
                {
                    if (id.is_number_integer())
                        market.label_ids.push_back(id.get<std::int64_t>());
                }
            }
            if (object.contains("childMarkets") && object["childMarkets"].is_array())
            {
                for (const auto &child : object["childMarkets"])
                    market.child_markets.push_back(parse_child(child));
            }
            return market;
        }

        BookLevel parse_level(const json &object)
        {
            return BookLevel{jstr(object, "price"), jstr(object, "size")};
        }

        OrderRecord parse_order(const json &object)
        {
            OrderRecord order;
            order.order_id = jstr(object, "orderId");
            if (order.order_id.empty())
                order.order_id = jstr(object, "transNo");
            order.status = jint32(object, "status");
            order.status_enum = jstr(object, "statusEnum");
            order.market_id = jint(object, "marketId");
            order.market_title = jstr(object, "marketTitle");
            order.side = jint32(object, "side");
            order.side_enum = jstr(object, "sideEnum");
            order.trading_method = jint32(object, "tradingMethod");
            order.trading_method_enum = jstr(object, "tradingMethodEnum");
            order.outcome = jstr(object, "outcome");
            order.price = jstr(object, "price");
            order.order_shares = jstr(object, "orderShares");
            order.order_amount = jstr(object, "orderAmount");
            order.filled_shares = jstr(object, "filledShares");
            order.filled_amount = jstr(object, "filledAmount");
            order.quote_token = jstr(object, "quoteToken");
            order.created_at = jint(object, "createdAt");
            if (object.contains("postOnly") && object["postOnly"].is_boolean())
                order.post_only = object["postOnly"].get<bool>();
            order.raw_json = object.dump();
            return order;
        }

        QuoteToken parse_quote(const json &object)
        {
            QuoteToken token;
            token.id = jint(object, "id");
            token.quote_token_name = jstr(object, "quoteTokenName");
            token.quote_token_address = jstr(object, "quoteTokenAddress");
            token.ctf_exchange_address = jstr(object, "ctfExchangeAddress");
            token.decimal = jint32(object, "decimal");
            token.symbol = jstr(object, "symbol");
            token.chain_id = jstr(object, "chainId");
            token.created_at = jint(object, "createdAt");
            return token;
        }

        std::string lower_copy(std::string value)
        {
            for (char &c : value)
            {
                if (c >= 'A' && c <= 'Z')
                    c = static_cast<char>(c - 'A' + 'a');
            }
            return value;
        }

        std::string unix_seconds_now()
        {
            const auto now = std::chrono::system_clock::now().time_since_epoch();
            return std::to_string(std::chrono::duration_cast<std::chrono::seconds>(now).count());
        }

        bool amount_set(const std::optional<std::string> &value)
        {
            return value.has_value() && !value->empty();
        }
    } // namespace

    ClobClient::ClobClient(Environment environment) : ClobClient(std::move(environment), {}) {}

    ClobClient::ClobClient(Environment environment, std::string api_key)
        : environment_(std::move(environment)), api_key_(std::move(api_key))
    {
        environment_.validate();
        http_.set_base_url(environment_.rest_base);
        if (!environment_.proxy_url.empty())
            http_.set_proxy(environment_.proxy_url);
        apply_api_key_header();
    }

    ClobClient::~ClobClient() = default;
    ClobClient::ClobClient(ClobClient &&) noexcept = default;
    ClobClient &ClobClient::operator=(ClobClient &&) noexcept = default;

    void ClobClient::set_api_key(std::string api_key)
    {
        api_key_ = std::move(api_key);
        apply_api_key_header();
    }

    void ClobClient::set_signer(std::unique_ptr<OrderSigner> signer) { signer_ = std::move(signer); }

    void ClobClient::apply_api_key_header()
    {
        if (api_key_.empty())
            http_.clear_header("apikey");
        else
            http_.set_header("apikey", api_key_);
    }

    Result<MarketList> ClobClient::list_markets(const MarketQuery &query)
    {
        Query q;
        if (query.page)
            q.add("page", *query.page);
        if (query.limit)
            q.add("limit", *query.limit);
        if (query.status)
            q.add("status", *query.status);
        if (query.market_type)
            q.add("marketType", *query.market_type);
        if (query.sort_by)
            q.add("sortBy", *query.sort_by);
        if (query.chain_id)
            q.add("chainId", *query.chain_id);
        if (query.label_id)
            q.add("labelId", *query.label_id);
        const std::string path = q.path("/market");
        auto envelope = parse_envelope(http_.get(path), path);
        if (!envelope)
            return Result<MarketList>::failure(envelope.error());
        MarketList list;
        list.total = jint(envelope.value().result, "total");
        if (envelope.value().result.contains("list") && envelope.value().result["list"].is_array())
        {
            for (const auto &item : envelope.value().result["list"])
                list.list.push_back(parse_market(item));
        }
        return Result<MarketList>::success(std::move(list));
    }

    Result<Market> ClobClient::get_market(std::int64_t market_id)
    {
        const std::string path = "/market/" + std::to_string(market_id);
        auto envelope = parse_envelope(http_.get(path), path);
        if (!envelope)
            return Result<Market>::failure(envelope.error());
        const json &result = envelope.value().result;
        if (!result.contains("data") || !result["data"].is_object())
            return Result<Market>::failure(make_parse_error("market detail missing result.data", path));
        return Result<Market>::success(parse_market(result["data"]));
    }

    Result<Market> ClobClient::get_categorical_market(std::int64_t market_id)
    {
        const std::string path = "/market/categorical/" + std::to_string(market_id);
        auto envelope = parse_envelope(http_.get(path), path);
        if (!envelope)
            return Result<Market>::failure(envelope.error());
        const json &result = envelope.value().result;
        if (!result.contains("data") || !result["data"].is_object())
            return Result<Market>::failure(make_parse_error("categorical market missing result.data", path));
        return Result<Market>::success(parse_market(result["data"]));
    }

    Result<Market> ClobClient::get_market_by_slug(std::string_view slug)
    {
        const std::string path = "/market/slug/" + url_encode(slug);
        auto envelope = parse_envelope(http_.get(path), path);
        if (!envelope)
            return Result<Market>::failure(envelope.error());
        const json &result = envelope.value().result;
        if (!result.contains("data") || !result["data"].is_object())
            return Result<Market>::failure(make_parse_error("slug lookup missing result.data", path));
        return Result<Market>::success(parse_market(result["data"]));
    }

    Result<std::vector<Label>> ClobClient::list_labels()
    {
        const std::string path = "/label";
        auto envelope = parse_envelope(http_.get(path), path);
        if (!envelope)
            return Result<std::vector<Label>>::failure(envelope.error());
        std::vector<Label> labels;
        if (envelope.value().result.contains("list") && envelope.value().result["list"].is_array())
        {
            for (const auto &item : envelope.value().result["list"])
            {
                Label label;
                label.label_id = jint(item, "labelId");
                label.label_name = jstr(item, "labelName");
                label.image_url = jstr(item, "imageUrl");
                label.image_mobile_url = jstr(item, "imageMobileUrl");
                labels.push_back(std::move(label));
            }
        }
        return Result<std::vector<Label>>::success(std::move(labels));
    }

    Result<OrderBook> ClobClient::get_orderbook(std::string_view token_id)
    {
        if (token_id.empty())
            return Result<OrderBook>::failure(make_invalid_argument("token_id is required", "/token/orderbook"));
        const std::string path = "/token/orderbook?token_id=" + url_encode(token_id);
        auto envelope = parse_envelope(http_.get(path), path);
        if (!envelope)
            return Result<OrderBook>::failure(envelope.error());
        const json &result = envelope.value().result;
        OrderBook book;
        book.market = jstr(result, "market");
        book.token_id = jstr(result, "tokenId");
        book.timestamp_ms = jint(result, "timestamp");
        if (result.contains("bids") && result["bids"].is_array())
        {
            for (const auto &level : result["bids"])
                book.bids.push_back(parse_level(level));
        }
        if (result.contains("asks") && result["asks"].is_array())
        {
            for (const auto &level : result["asks"])
                book.asks.push_back(parse_level(level));
        }
        return Result<OrderBook>::success(std::move(book));
    }

    Result<LatestPrice> ClobClient::get_latest_price(std::string_view token_id)
    {
        if (token_id.empty())
            return Result<LatestPrice>::failure(make_invalid_argument("token_id is required", "/token/latest-price"));
        const std::string path = "/token/latest-price?token_id=" + url_encode(token_id);
        auto envelope = parse_envelope(http_.get(path), path);
        if (!envelope)
            return Result<LatestPrice>::failure(envelope.error());
        const json &result = envelope.value().result;
        LatestPrice price;
        price.token_id = jstr(result, "tokenId");
        price.price = jstr(result, "price");
        price.side = jstr(result, "side");
        price.size = jstr(result, "size");
        price.timestamp_ms = jint(result, "timestamp");
        return Result<LatestPrice>::success(std::move(price));
    }

    Result<PriceHistory> ClobClient::get_price_history(const PriceHistoryQuery &query)
    {
        if (query.token_id.empty())
            return Result<PriceHistory>::failure(make_invalid_argument("token_id is required", "/token/price-history"));
        Query q;
        q.add("token_id", query.token_id);
        if (query.interval)
            q.add("interval", *query.interval);
        if (query.start_at)
            q.add("start_at", *query.start_at);
        if (query.end_at)
            q.add("end_at", *query.end_at);
        const std::string path = q.path("/token/price-history");
        auto envelope = parse_envelope(http_.get(path), path);
        if (!envelope)
            return Result<PriceHistory>::failure(envelope.error());
        PriceHistory history;
        if (envelope.value().result.contains("history") && envelope.value().result["history"].is_array())
        {
            for (const auto &point : envelope.value().result["history"])
                history.history.push_back(PricePoint{jint(point, "t"), jstr(point, "p")});
        }
        return Result<PriceHistory>::success(std::move(history));
    }

    Result<std::vector<QuoteToken>> ClobClient::list_quote_tokens(const QuoteTokenQuery &query)
    {
        Query q;
        if (query.page)
            q.add("page", *query.page);
        if (query.limit)
            q.add("limit", *query.limit);
        if (query.quote_token_name)
            q.add("quoteTokenName", *query.quote_token_name);
        if (query.chain_id)
            q.add("chainId", *query.chain_id);
        const std::string path = q.path("/quoteToken");
        auto envelope = parse_envelope(http_.get(path), path);
        if (!envelope)
            return Result<std::vector<QuoteToken>>::failure(envelope.error());
        std::vector<QuoteToken> tokens;
        if (envelope.value().result.contains("list") && envelope.value().result["list"].is_array())
        {
            for (const auto &item : envelope.value().result["list"])
                tokens.push_back(parse_quote(item));
        }
        return Result<std::vector<QuoteToken>>::success(std::move(tokens));
    }

    Result<ApiKeyCredential> ClobClient::create_api_key(std::string timestamp)
    {
        return [&]() -> Result<ApiKeyCredential> {
            if (!signer_)
                return Result<ApiKeyCredential>::failure(make_signing_error("set_signer is required to mint an API key"));
            if (timestamp.empty())
                timestamp = unix_seconds_now();
            ApiKeyHeaders headers;
            try
            {
                headers = signer_->sign_api_key_auth("create", timestamp, environment_.chain_id);
            }
            catch (const std::exception &ex)
            {
                return Result<ApiKeyCredential>::failure(make_signing_error(ex.what()));
            }
            // Auth calls must not send a stale apikey. Wallet headers only.
            http_.clear_header("apikey");
            http_.set_header("OPINION_ADDRESS", headers.address);
            http_.set_header("OPINION_SIGNATURE", headers.signature);
            http_.set_header("OPINION_TIMESTAMP", headers.timestamp);
            auto envelope = parse_envelope(http_.post("/auth/api-key", {}), "/auth/api-key");
            http_.clear_header("OPINION_ADDRESS");
            http_.clear_header("OPINION_SIGNATURE");
            http_.clear_header("OPINION_TIMESTAMP");
            apply_api_key_header();
            if (!envelope)
                return Result<ApiKeyCredential>::failure(envelope.error());
            ApiKeyCredential credential;
            credential.api_key = jstr(envelope.value().result, "apiKey");
            credential.wallet_address = jstr(envelope.value().result, "walletAddress");
            return Result<ApiKeyCredential>::success(std::move(credential));
        }();
    }

    Result<ApiKeyCredential> ClobClient::get_api_key(std::string timestamp)
    {
        if (!signer_)
            return Result<ApiKeyCredential>::failure(make_signing_error("set_signer is required to read an API key"));
        if (timestamp.empty())
            timestamp = unix_seconds_now();
        ApiKeyHeaders headers;
        try
        {
            headers = signer_->sign_api_key_auth("get", timestamp, environment_.chain_id);
        }
        catch (const std::exception &ex)
        {
            return Result<ApiKeyCredential>::failure(make_signing_error(ex.what()));
        }
        http_.clear_header("apikey");
        http_.set_header("OPINION_ADDRESS", headers.address);
        http_.set_header("OPINION_SIGNATURE", headers.signature);
        http_.set_header("OPINION_TIMESTAMP", headers.timestamp);
        auto envelope = parse_envelope(http_.get("/auth/api-key"), "/auth/api-key");
        http_.clear_header("OPINION_ADDRESS");
        http_.clear_header("OPINION_SIGNATURE");
        http_.clear_header("OPINION_TIMESTAMP");
        apply_api_key_header();
        if (!envelope)
            return Result<ApiKeyCredential>::failure(envelope.error());
        ApiKeyCredential credential;
        credential.api_key = jstr(envelope.value().result, "apiKey");
        credential.wallet_address = jstr(envelope.value().result, "walletAddress");
        return Result<ApiKeyCredential>::success(std::move(credential));
    }

    Result<bool> ClobClient::delete_api_key(std::string timestamp)
    {
        if (!signer_)
            return Result<bool>::failure(make_signing_error("set_signer is required to delete an API key"));
        if (timestamp.empty())
            timestamp = unix_seconds_now();
        ApiKeyHeaders headers;
        try
        {
            headers = signer_->sign_api_key_auth("delete", timestamp, environment_.chain_id);
        }
        catch (const std::exception &ex)
        {
            return Result<bool>::failure(make_signing_error(ex.what()));
        }
        http_.clear_header("apikey");
        http_.set_header("OPINION_ADDRESS", headers.address);
        http_.set_header("OPINION_SIGNATURE", headers.signature);
        http_.set_header("OPINION_TIMESTAMP", headers.timestamp);
        auto envelope = parse_envelope(http_.del("/auth/api-key"), "/auth/api-key");
        http_.clear_header("OPINION_ADDRESS");
        http_.clear_header("OPINION_SIGNATURE");
        http_.clear_header("OPINION_TIMESTAMP");
        apply_api_key_header();
        if (!envelope)
            return Result<bool>::failure(envelope.error());
        if (envelope.value().result.contains("deleted") && envelope.value().result["deleted"].is_boolean())
            return Result<bool>::success(envelope.value().result["deleted"].get<bool>());
        return Result<bool>::success(true);
    }

    Result<OrderList> ClobClient::list_orders(const OrderQuery &query)
    {
        if (api_key_.empty())
            return Result<OrderList>::failure(make_auth_error("apikey is required", "/order"));
        Query q;
        if (query.page)
            q.add("page", *query.page);
        if (query.limit)
            q.add("limit", *query.limit);
        if (query.market_id)
            q.add("marketId", *query.market_id);
        if (query.chain_id)
            q.add("chainId", *query.chain_id);
        if (query.status)
            q.add("status", *query.status);
        const std::string path = q.path("/order");
        auto envelope = parse_envelope(http_.get(path), path);
        if (!envelope)
            return Result<OrderList>::failure(envelope.error());
        OrderList list;
        list.total = jint(envelope.value().result, "total");
        if (envelope.value().result.contains("list") && envelope.value().result["list"].is_array())
        {
            for (const auto &item : envelope.value().result["list"])
                list.list.push_back(parse_order(item));
        }
        return Result<OrderList>::success(std::move(list));
    }

    Result<OrderRecord> ClobClient::get_order(std::string_view order_id)
    {
        if (api_key_.empty())
            return Result<OrderRecord>::failure(make_auth_error("apikey is required", "/order/{orderId}"));
        if (order_id.empty())
            return Result<OrderRecord>::failure(make_invalid_argument("order_id is required", "/order/{orderId}"));
        const std::string path = "/order/" + url_encode(order_id);
        auto envelope = parse_envelope(http_.get(path), path);
        if (!envelope)
            return Result<OrderRecord>::failure(envelope.error());
        const json &result = envelope.value().result;
        if (!result.contains("orderData") || !result["orderData"].is_object())
            return Result<OrderRecord>::failure(make_parse_error("order detail missing result.orderData", path));
        return Result<OrderRecord>::success(parse_order(result["orderData"]));
    }

    Result<PlaceOrderResult> ClobClient::place_order(const PlaceOrderRequest &request)
    {
        const std::string endpoint = "/order";
        if (api_key_.empty())
            return Result<PlaceOrderResult>::failure(make_auth_error("apikey is required", endpoint));
        if (!signer_)
            return Result<PlaceOrderResult>::failure(make_signing_error("set_signer is required to place an order"));
        if (request.check_approval)
            return Result<PlaceOrderResult>::failure(make_invalid_argument(
                "on-chain enableTrading is not implemented in v1; pass check_approval=false", endpoint));
        if (request.market_id <= 0)
            return Result<PlaceOrderResult>::failure(make_invalid_argument("market_id is required", endpoint));
        if (request.token_id.empty())
            return Result<PlaceOrderResult>::failure(make_invalid_argument("token_id is required", endpoint));
        if (request.maker.empty())
            return Result<PlaceOrderResult>::failure(make_invalid_argument(
                "maker (Safe / multiSig address) is required; official SDKs sign with signatureType POLY_GNOSIS_SAFE",
                endpoint));

        const bool quote = amount_set(request.maker_amount_in_quote_token);
        const bool base = amount_set(request.maker_amount_in_base_token);
        if (quote == base)
            return Result<PlaceOrderResult>::failure(make_invalid_argument(
                "set exactly one of maker_amount_in_quote_token or maker_amount_in_base_token", endpoint));

        if (request.order_type == OrderType::Market && request.side == OrderSide::Buy && base)
            return Result<PlaceOrderResult>::failure(
                make_invalid_argument("maker_amount_in_base_token is not allowed for a market buy", endpoint));
        if (request.order_type == OrderType::Market && request.side == OrderSide::Sell && quote)
            return Result<PlaceOrderResult>::failure(
                make_invalid_argument("maker_amount_in_quote_token is not allowed for a market sell", endpoint));

        std::string price = request.price;
        if (request.order_type == OrderType::Limit)
        {
            std::string reason;
            if (!valid_limit_price(price, &reason))
                return Result<PlaceOrderResult>::failure(make_invalid_argument(reason, endpoint));
        }
        else if (request.order_type == OrderType::Market)
        {
            price = "0";
        }
        else
        {
            return Result<PlaceOrderResult>::failure(make_invalid_argument("order_type must be market (1) or limit (2)", endpoint));
        }

        const std::string &human = quote ? *request.maker_amount_in_quote_token : *request.maker_amount_in_base_token;
        try
        {
            if (!decimal_at_least_one(human))
                return Result<PlaceOrderResult>::failure(
                    make_invalid_argument("maker amount must be at least 1", endpoint));
        }
        catch (const std::exception &ex)
        {
            return Result<PlaceOrderResult>::failure(make_invalid_argument(ex.what(), endpoint));
        }

        auto market = get_market(request.market_id);
        if (!market)
            return Result<PlaceOrderResult>::failure(market.error());
        auto quotes = list_quote_tokens(QuoteTokenQuery{});
        if (!quotes)
            return Result<PlaceOrderResult>::failure(quotes.error());

        const std::string want = lower_copy(market.value().quote_token);
        const QuoteToken *quote_token = nullptr;
        for (const auto &token : quotes.value())
        {
            if (lower_copy(token.quote_token_address) == want)
            {
                quote_token = &token;
                break;
            }
        }
        if (!quote_token)
            return Result<PlaceOrderResult>::failure(
                make_invalid_argument("quote token from the market was not in GET /quoteToken", endpoint));
        if (quote_token->ctf_exchange_address.empty())
            return Result<PlaceOrderResult>::failure(
                make_invalid_argument("quote token is missing ctfExchangeAddress", endpoint));

        int chain = 0;
        try
        {
            chain = std::stoi(market.value().chain_id);
        }
        catch (...)
        {
            return Result<PlaceOrderResult>::failure(
                make_parse_error("market chainId is not an integer", endpoint));
        }
        if (chain != environment_.chain_id)
            return Result<PlaceOrderResult>::failure(
                make_invalid_argument("market chainId does not match the client environment", endpoint));

        std::string maker_wei;
        std::string taker_wei = "0";
        try
        {
            if (request.order_type == OrderType::Market)
            {
                maker_wei = decimal_to_scaled_integer(human, quote_token->decimal);
            }
            else if (request.side == OrderSide::Buy && base)
            {
                maker_wei = quote_wei_from_base(*request.maker_amount_in_base_token, price, quote_token->decimal);
            }
            else if (request.side == OrderSide::Sell && quote)
            {
                maker_wei = base_wei_from_quote(*request.maker_amount_in_quote_token, price, quote_token->decimal);
            }
            else
            {
                maker_wei = decimal_to_scaled_integer(human, quote_token->decimal);
            }
            if (request.order_type == OrderType::Limit)
            {
                const LimitAmounts amounts =
                    limit_order_amounts(maker_wei, price, request.side == OrderSide::Buy);
                maker_wei = amounts.maker_amount;
                taker_wei = amounts.taker_amount;
            }
        }
        catch (const std::exception &ex)
        {
            return Result<PlaceOrderResult>::failure(make_invalid_argument(ex.what(), endpoint));
        }
        if (maker_wei == "0")
            return Result<PlaceOrderResult>::failure(make_invalid_argument("calculated maker amount is zero", endpoint));

        UnsignedOrder unsigned_order;
        unsigned_order.salt = OrderSigner::random_salt();
        unsigned_order.maker = request.maker;
        unsigned_order.signer = signer_->address();
        unsigned_order.token_id = request.token_id;
        unsigned_order.maker_amount = maker_wei;
        unsigned_order.taker_amount = taker_wei;
        unsigned_order.side = request.side;
        unsigned_order.signature_type = SignatureType::PolyGnosisSafe;
        unsigned_order.verifying_contract = quote_token->ctf_exchange_address;
        unsigned_order.chain_id = environment_.chain_id;

        SignedOrder signed_order;
        try
        {
            signed_order = signer_->sign_order(unsigned_order);
        }
        catch (const std::exception &ex)
        {
            return Result<PlaceOrderResult>::failure(make_signing_error(ex.what()));
        }

        const auto now = std::chrono::system_clock::now().time_since_epoch();
        const auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(now).count();

        json body = {
            {"salt", signed_order.order.salt},
            {"topicId", request.market_id},
            {"maker", signed_order.order.maker},
            {"signer", signed_order.order.signer},
            {"taker", signed_order.order.taker},
            {"tokenId", signed_order.order.token_id},
            {"makerAmount", signed_order.order.maker_amount},
            {"takerAmount", signed_order.order.taker_amount},
            {"expiration", signed_order.order.expiration},
            {"nonce", signed_order.order.nonce},
            {"feeRateBps", signed_order.order.fee_rate_bps},
            {"side", std::to_string(static_cast<int>(signed_order.order.side))},
            {"signatureType", std::to_string(static_cast<int>(signed_order.order.signature_type))},
            {"signature", signed_order.signature},
            {"sign", signed_order.signature},
            {"contractAddress", ""},
            {"currencyAddress", quote_token->quote_token_address},
            {"price", price},
            {"tradingMethod", static_cast<int>(request.order_type)},
            {"timestamp", timestamp},
            {"safeRate", "0"},
            {"orderExpTime", "0"},
        };
        if (request.post_only.has_value())
            body["postOnly"] = *request.post_only;

        auto envelope = parse_envelope(http_.post(endpoint, body.dump()), endpoint);
        if (!envelope)
            return Result<PlaceOrderResult>::failure(envelope.error());

        PlaceOrderResult placed;
        placed.signed_order = std::move(signed_order);
        placed.raw_result_json = envelope.value().result.dump();
        if (envelope.value().result.contains("orderData") && envelope.value().result["orderData"].is_object())
            placed.order = parse_order(envelope.value().result["orderData"]);
        return Result<PlaceOrderResult>::success(std::move(placed));
    }

    Result<CancelOrderResult> ClobClient::cancel_order(std::string_view order_id)
    {
        const std::string endpoint = "/order/cancel";
        if (api_key_.empty())
            return Result<CancelOrderResult>::failure(make_auth_error("apikey is required", endpoint));
        if (order_id.empty())
            return Result<CancelOrderResult>::failure(make_invalid_argument("order_id is required", endpoint));
        json body = {{"orderId", std::string(order_id)}};
        auto envelope = parse_envelope(http_.post(endpoint, body.dump()), endpoint);
        if (!envelope)
            return Result<CancelOrderResult>::failure(envelope.error());
        CancelOrderResult result;
        result.raw_result_json = envelope.value().result.dump();
        return Result<CancelOrderResult>::success(std::move(result));
    }

    Result<PositionList> ClobClient::list_positions(std::string_view wallet, const PositionQuery &query)
    {
        if (api_key_.empty())
            return Result<PositionList>::failure(make_auth_error("apikey is required", "/positions/user/{walletAddress}"));
        if (wallet.empty())
            return Result<PositionList>::failure(
                make_invalid_argument("wallet address is required", "/positions/user/{walletAddress}"));
        Query q;
        if (query.page)
            q.add("page", *query.page);
        if (query.limit)
            q.add("limit", *query.limit);
        if (query.market_id)
            q.add("marketId", *query.market_id);
        if (query.chain_id)
            q.add("chainId", *query.chain_id);
        const std::string path = q.path("/positions/user/" + url_encode(wallet));
        auto envelope = parse_envelope(http_.get(path), path);
        if (!envelope)
            return Result<PositionList>::failure(envelope.error());
        PositionList list;
        list.total = jint(envelope.value().result, "total");
        if (envelope.value().result.contains("list") && envelope.value().result["list"].is_array())
        {
            for (const auto &item : envelope.value().result["list"])
            {
                Position position;
                position.market_id = jint(item, "marketId");
                position.market_title = jstr(item, "marketTitle");
                position.market_status = jint32(item, "marketStatus");
                position.outcome = jstr(item, "outcome");
                position.outcome_side = jint32(item, "outcomeSide");
                position.shares_owned = jstr(item, "sharesOwned");
                position.token_id = jstr(item, "tokenId");
                position.avg_entry_price = jstr(item, "avgEntryPrice");
                position.quote_token = jstr(item, "quoteToken");
                position.raw_json = item.dump();
                list.list.push_back(std::move(position));
            }
        }
        return Result<PositionList>::success(std::move(list));
    }
} // namespace opinion
