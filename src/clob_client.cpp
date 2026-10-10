#include "opinion/clob_client.hpp"
#include "book_decimal.hpp"
#include "rest_result.hpp"

#include "opinion/decimal_math.hpp"

#include <nlohmann/json.hpp>

#include <cctype>
#include <chrono>
#include <fstream>
#include <sstream>

namespace opinion
{
    namespace
    {
        using json = nlohmann::json;

        std::string url_encode(std::string_view value)
        {
            static constexpr char hex[] = "0123456789ABCDEF";
            std::string out;
            for (unsigned char byte : value)
            {
                if (std::isalnum(byte) || byte == '-' || byte == '_' || byte == '.' || byte == '~')
                {
                    out.push_back(static_cast<char>(byte));
                }
                else
                {
                    out.push_back('%');
                    out.push_back(hex[byte >> 4]);
                    out.push_back(hex[byte & 0x0f]);
                }
            }
            return out;
        }

        std::string with_query(std::string url, const std::map<std::string, std::string> &query)
        {
            if (query.empty())
            {
                return url;
            }
            url.push_back('?');
            bool first = true;
            for (const auto &[key, value] : query)
            {
                if (!first)
                {
                    url.push_back('&');
                }
                first = false;
                url += url_encode(key);
                url.push_back('=');
                url += url_encode(value);
            }
            return url;
        }

        std::string excerpt_of(const std::string &body)
        {
            return body.substr(0, std::min<std::size_t>(body.size(), 512));
        }

        std::string jstr(const json &object, const char *key)
        {
            if (!object.contains(key) || object[key].is_null())
            {
                return {};
            }
            const auto &value = object[key];
            if (value.is_string())
            {
                return value.get<std::string>();
            }
            if (value.is_number_integer())
            {
                return std::to_string(value.get<std::int64_t>());
            }
            if (value.is_number_unsigned())
            {
                return std::to_string(value.get<std::uint64_t>());
            }
            if (value.is_boolean())
            {
                return value.get<bool>() ? "true" : "false";
            }
            if (value.is_number())
            {
                return value.dump();
            }
            return {};
        }

        std::int64_t ji64(const json &object, const char *key)
        {
            if (!object.contains(key) || object[key].is_null())
            {
                return 0;
            }
            const auto &value = object[key];
            if (value.is_number_integer())
            {
                return value.get<std::int64_t>();
            }
            if (value.is_string())
            {
                try
                {
                    return std::stoll(value.get<std::string>());
                }
                catch (const std::exception &)
                {
                    return 0;
                }
            }
            return 0;
        }

        int jint(const json &object, const char *key)
        {
            return static_cast<int>(ji64(object, key));
        }

        void add_page(std::map<std::string, std::string> &query, const PageQuery &page)
        {
            if (page.page)
            {
                query["page"] = std::to_string(*page.page);
            }
            if (page.limit)
            {
                query["limit"] = std::to_string(*page.limit);
            }
            if (page.market_id)
            {
                query["marketId"] = std::to_string(*page.market_id);
            }
            if (page.chain_id)
            {
                query["chainId"] = *page.chain_id;
            }
            if (page.status)
            {
                query["status"] = *page.status;
            }
        }

        SdkError check_limit(const std::optional<int> &limit)
        {
            if (limit && (*limit < 1 || *limit > 20))
            {
                return make_invalid_argument("limit must be between 1 and 20");
            }
            return {};
        }

        Market parse_market(const json &object)
        {
            Market market;
            market.market_id = ji64(object, "marketId");
            market.market_title = jstr(object, "marketTitle");
            market.slug = jstr(object, "slug");
            market.status = jint(object, "status");
            market.status_enum = jstr(object, "statusEnum");
            market.market_type = jint(object, "marketType");
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
            market.created_at = ji64(object, "createdAt");
            market.cutoff_at = ji64(object, "cutoffAt");
            market.resolved_at = ji64(object, "resolvedAt");
            if (object.contains("labels") && object["labels"].is_array())
            {
                for (const auto &label : object["labels"])
                {
                    if (label.is_string())
                    {
                        market.labels.push_back(label.get<std::string>());
                    }
                }
            }
            if (object.contains("labelIds") && object["labelIds"].is_array())
            {
                for (const auto &label_id : object["labelIds"])
                {
                    if (label_id.is_number_integer())
                    {
                        market.label_ids.push_back(label_id.get<std::int64_t>());
                    }
                }
            }
            if (object.contains("resolution") && object["resolution"].is_object())
            {
                ResolutionSummary resolution;
                resolution.phase = jstr(object["resolution"], "phase");
                resolution.proposed_token_id = jstr(object["resolution"], "proposedTokenId");
                if (object["resolution"].contains("disputeDeadline") && !object["resolution"]["disputeDeadline"].is_null())
                {
                    resolution.has_dispute_deadline = true;
                    resolution.dispute_deadline = ji64(object["resolution"], "disputeDeadline");
                }
                market.resolution = resolution;
            }
            if (object.contains("childMarkets") && object["childMarkets"].is_array())
            {
                for (const auto &child : object["childMarkets"])
                {
                    if (child.is_object())
                    {
                        market.child_markets.push_back(parse_market(child));
                    }
                }
            }
            return market;
        }

        Market parse_market_detail(const json &result)
        {
            if (result.contains("data") && result["data"].is_object())
            {
                return parse_market(result["data"]);
            }
            return parse_market(result);
        }

        OrderbookLevel parse_level(const json &object)
        {
            OrderbookLevel level{jstr(object, "price"), jstr(object, "size")};
            detail::decimal_view(level.price);
            detail::decimal_view(level.size);
            return level;
        }

        OrderTrade parse_trade(const json &object)
        {
            OrderTrade trade;
            trade.order_no = jstr(object, "orderNo");
            if (trade.order_no.empty())
            {
                trade.order_no = jstr(object, "orderId");
            }
            trade.trade_no = jstr(object, "tradeNo");
            trade.tx_hash = jstr(object, "txHash");
            trade.market_id = ji64(object, "marketId");
            trade.market_title = jstr(object, "marketTitle");
            trade.root_market_id = ji64(object, "rootMarketId");
            trade.root_market_title = jstr(object, "rootMarketTitle");
            trade.side = jstr(object, "side");
            trade.outcome = jstr(object, "outcome");
            trade.outcome_side = jint(object, "outcomeSide");
            trade.price = jstr(object, "price");
            trade.shares = jstr(object, "shares");
            trade.amount = jstr(object, "amount");
            trade.fee = jstr(object, "fee");
            trade.fee_formatted = jstr(object, "feeFormatted");
            trade.profit = jstr(object, "profit");
            trade.quote_token = jstr(object, "quoteToken");
            trade.quote_token_usd_price = jstr(object, "quoteTokenUsdPrice");
            trade.usd_amount = jstr(object, "usdAmount");
            trade.status = jint(object, "status");
            trade.status_enum = jstr(object, "statusEnum");
            trade.chain_id = jstr(object, "chainId");
            trade.created_at = ji64(object, "createdAt");
            return trade;
        }

        OrderRecord parse_order(const json &object)
        {
            OrderRecord order;
            order.order_id = jstr(object, "orderId");
            if (order.order_id.empty())
            {
                order.order_id = jstr(object, "transNo");
            }
            order.status = jint(object, "status");
            order.status_enum = jstr(object, "statusEnum");
            order.market_id = ji64(object, "marketId");
            order.market_title = jstr(object, "marketTitle");
            order.root_market_id = ji64(object, "rootMarketId");
            order.root_market_title = jstr(object, "rootMarketTitle");
            order.side = jint(object, "side");
            order.side_enum = jstr(object, "sideEnum");
            order.trading_method = jint(object, "tradingMethod");
            order.trading_method_enum = jstr(object, "tradingMethodEnum");
            order.trading_unit = jint(object, "tradingUnit");
            order.outcome = jstr(object, "outcome");
            order.outcome_side = jint(object, "outcomeSide");
            order.price = jstr(object, "price");
            order.order_shares = jstr(object, "orderShares");
            order.order_amount = jstr(object, "orderAmount");
            order.filled_shares = jstr(object, "filledShares");
            order.filled_amount = jstr(object, "filledAmount");
            order.profit = jstr(object, "profit");
            order.quote_token = jstr(object, "quoteToken");
            order.created_at = ji64(object, "createdAt");
            order.expires_at = ji64(object, "expiresAt");
            if (object.contains("postOnly") && object["postOnly"].is_boolean())
            {
                order.has_post_only = true;
                order.post_only = object["postOnly"].get<bool>();
            }
            if (object.contains("trades") && object["trades"].is_array())
            {
                for (const auto &trade : object["trades"])
                {
                    if (trade.is_object())
                    {
                        order.trades.push_back(parse_trade(trade));
                    }
                }
            }
            return order;
        }

        Position parse_position(const json &object)
        {
            Position position;
            position.market_id = ji64(object, "marketId");
            position.market_title = jstr(object, "marketTitle");
            position.market_status = jint(object, "marketStatus");
            position.market_status_enum = jstr(object, "marketStatusEnum");
            position.market_cutoff_at = ji64(object, "marketCutoffAt");
            position.root_market_id = ji64(object, "rootMarketId");
            position.root_market_title = jstr(object, "rootMarketTitle");
            position.outcome = jstr(object, "outcome");
            position.outcome_side = jint(object, "outcomeSide");
            position.shares_owned = jstr(object, "sharesOwned");
            position.shares_frozen = jstr(object, "sharesFrozen");
            position.unrealized_pnl = jstr(object, "unrealizedPnl");
            position.unrealized_pnl_percent = jstr(object, "unrealizedPnlPercent");
            position.daily_pnl_change = jstr(object, "dailyPnlChange");
            position.daily_pnl_change_percent = jstr(object, "dailyPnlChangePercent");
            position.condition_id = jstr(object, "conditionId");
            position.token_id = jstr(object, "tokenId");
            position.current_value_in_quote_token = jstr(object, "currentValueInQuoteToken");
            position.avg_entry_price = jstr(object, "avgEntryPrice");
            position.claim_status = jint(object, "claimStatus");
            position.claim_status_enum = jstr(object, "claimStatusEnum");
            position.quote_token = jstr(object, "quoteToken");
            return position;
        }

        QuoteToken parse_quote_token(const json &object)
        {
            QuoteToken token;
            token.id = ji64(object, "id");
            token.name = jstr(object, "quoteTokenName");
            token.address = jstr(object, "quoteTokenAddress");
            token.ctf_exchange_address = jstr(object, "ctfExchangeAddress");
            token.decimals = jint(object, "decimal");
            token.symbol = jstr(object, "symbol");
            token.chain_id = jstr(object, "chainId");
            token.created_at = ji64(object, "createdAt");
            return token;
        }

        std::string lower_copy(std::string value)
        {
            for (char &character : value)
            {
                character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
            }
            return value;
        }

        struct ParsedAmount
        {
            std::string digits{"0"};
            int scale{0};
        };

        ParsedAmount parse_amount(std::string_view text)
        {
            std::string value(text);
            if (!value.empty() && value.front() == '+')
            {
                value.erase(value.begin());
            }
            if (value.empty() || value.front() == '-')
            {
                throw std::invalid_argument("amount must be a positive decimal");
            }
            const auto dot = value.find('.');
            std::string whole = dot == std::string::npos ? value : value.substr(0, dot);
            std::string fraction = dot == std::string::npos ? std::string() : value.substr(dot + 1);
            if (whole.empty())
            {
                whole = "0";
            }
            if (fraction.find('.') != std::string::npos)
            {
                throw std::invalid_argument("amount has more than one decimal point");
            }
            ParsedAmount parsed;
            parsed.digits = mul_decimal(whole + fraction, "1");
            parsed.scale = static_cast<int>(fraction.size());
            return parsed;
        }

        std::string format_amount(std::string digits, int scale)
        {
            if (scale < 0)
            {
                digits.append(static_cast<std::size_t>(-scale), '0');
                scale = 0;
            }
            if (static_cast<int>(digits.size()) <= scale)
            {
                digits.insert(0, static_cast<std::size_t>(scale) - digits.size() + 1, '0');
            }
            if (scale == 0)
            {
                return digits;
            }
            digits.insert(digits.end() - scale, '.');
            while (!digits.empty() && digits.back() == '0')
            {
                digits.pop_back();
            }
            if (!digits.empty() && digits.back() == '.')
            {
                digits.pop_back();
            }
            return digits.empty() ? "0" : digits;
        }

        std::string multiply_amounts(std::string_view left, std::string_view right)
        {
            const auto a = parse_amount(left);
            const auto b = parse_amount(right);
            return format_amount(mul_decimal(a.digits, b.digits), a.scale + b.scale);
        }

        std::string divide_amounts(std::string_view numerator, std::string_view denominator)
        {
            const auto a = parse_amount(numerator);
            const auto b = parse_amount(denominator);
            if (b.digits == "0")
            {
                throw std::invalid_argument("division by zero");
            }
            constexpr int k_extra = 18;
            std::string num = a.digits;
            num.append(static_cast<std::size_t>(b.scale + k_extra), '0');
            std::string den = b.digits;
            den.append(static_cast<std::size_t>(a.scale), '0');
            return format_amount(div_decimal(num, den), k_extra);
        }

        int compare_amounts(std::string_view left, std::string_view right)
        {
            const auto a = parse_amount(left);
            const auto b = parse_amount(right);
            std::string ad = a.digits;
            std::string bd = b.digits;
            if (a.scale < b.scale)
            {
                ad.append(static_cast<std::size_t>(b.scale - a.scale), '0');
            }
            else if (b.scale < a.scale)
            {
                bd.append(static_cast<std::size_t>(a.scale - b.scale), '0');
            }
            return compare_decimal(ad, bd);
        }

        std::string random_salt()
        {
            std::array<std::uint8_t, 32> bytes{};
            std::ifstream urandom("/dev/urandom", std::ios::binary);
            if (!urandom.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
            {
                throw std::runtime_error("failed to read /dev/urandom");
            }
            return uint256_to_decimal(keccak256(bytes.data(), bytes.size()));
        }

        std::string human_maker_amount(const PlaceOrderRequest &request)
        {
            const bool has_quote = request.maker_amount_in_quote_token.has_value();
            const bool has_base = request.maker_amount_in_base_token.has_value();
            if (has_quote == has_base)
            {
                throw std::invalid_argument("set exactly one of maker_amount_in_quote_token or maker_amount_in_base_token");
            }
            if (request.order_type == OrderType::Market && request.side == OrderSide::Buy && has_base)
            {
                throw std::invalid_argument("maker_amount_in_base_token is not allowed for a market buy");
            }
            if (request.order_type == OrderType::Market && request.side == OrderSide::Sell && has_quote)
            {
                throw std::invalid_argument("maker_amount_in_quote_token is not allowed for a market sell");
            }
            if (request.post_only && request.order_type != OrderType::Limit)
            {
                throw std::invalid_argument("post_only is only supported for limit orders");
            }
            const std::string &provided = has_quote ? *request.maker_amount_in_quote_token : *request.maker_amount_in_base_token;
            if (compare_amounts(provided, "1") < 0)
            {
                throw std::invalid_argument("maker amount must be at least 1");
            }
            if (request.order_type == OrderType::Limit)
            {
                (void)price_fraction_6(request.price);
            }
            if (request.side == OrderSide::Buy)
            {
                if (has_base)
                {
                    return multiply_amounts(*request.maker_amount_in_base_token, request.price);
                }
                return *request.maker_amount_in_quote_token;
            }
            if (has_base)
            {
                return *request.maker_amount_in_base_token;
            }
            return divide_amounts(*request.maker_amount_in_quote_token, request.price);
        }
    } // namespace

    ClobClient::ClobClient(Environment environment, ClientConfig config)
        : environment_(std::move(environment)), api_key_(std::move(config.api_key)), http_(config.http),
          order_http_(std::move(config.http))
    {
        environment_.validate();
    }

    void ClobClient::set_api_key(std::string api_key) { api_key_ = std::move(api_key); }

    Result<json> ClobClient::call(const std::string &method, const std::string &logical_path,
                                  const std::map<std::string, std::string> &query, const std::string &body,
                                  bool authenticated, const std::map<std::string, std::string> &extra_headers) const
    {
        std::string url;
        try
        {
            url = with_query(environment_.url_for(logical_path), query);
        }
        catch (const std::exception &error)
        {
            return Result<json>::failure(make_invalid_argument(error.what()));
        }
        std::map<std::string, std::string> headers = extra_headers;
        if (authenticated)
        {
            if (api_key_.empty())
            {
                return Result<json>::failure(make_http_status_error(401, logical_path, {}, {}));
            }
            headers["apikey"] = api_key_;
        }
        if (!body.empty() && headers.find("Content-Type") == headers.end())
        {
            headers["Content-Type"] = "application/json";
        }
        auto &http =
            method == "POST" && (logical_path == "/order" || logical_path == "/order/cancel") ? order_http_ : http_;
        const HttpResponse response = http.request(method, url, body, headers);
        if (!response.error.empty())
        {
            return Result<json>::failure(make_transport_error(response.error, logical_path));
        }
        if (!response.ok())
        {
            std::string retry_after;
            const auto found = response.headers.find("retry-after");
            if (found != response.headers.end())
            {
                retry_after = found->second;
            }
            return Result<json>::failure(
                make_http_status_error(response.status_code, logical_path, response.body, retry_after));
        }
        try
        {
            auto envelope = detail::parse_envelope(response.body);
            if (envelope.code != 0)
            {
                return Result<json>::failure(
                    make_api_error(envelope.code, envelope.message, logical_path, response.status_code, response.body));
            }
            return Result<json>::success(std::move(envelope.result));
        }
        catch (const std::exception &error)
        {
            return Result<json>::failure(make_parse_error(error.what(), logical_path, excerpt_of(response.body)));
        }
    }

    Result<MarketList> ClobClient::list_markets(const MarketListQuery &query) const
    {
        if (const SdkError limit_error = check_limit(query.limit); !limit_error.message.empty())
        {
            return Result<MarketList>::failure(limit_error);
        }
        std::map<std::string, std::string> params;
        if (query.page)
        {
            params["page"] = std::to_string(*query.page);
        }
        if (query.limit)
        {
            params["limit"] = std::to_string(*query.limit);
        }
        if (query.status)
        {
            params["status"] = *query.status;
        }
        if (query.market_type)
        {
            params["marketType"] = std::to_string(*query.market_type);
        }
        if (query.sort_by)
        {
            params["sortBy"] = std::to_string(*query.sort_by);
        }
        if (query.chain_id)
        {
            params["chainId"] = *query.chain_id;
        }
        if (query.label_id)
        {
            params["labelId"] = std::to_string(*query.label_id);
        }
        if (query.collection_type)
        {
            params["collectionType"] = std::to_string(*query.collection_type);
        }
        auto raw = call("GET", "/market", params, {}, false);
        if (!raw)
        {
            return Result<MarketList>::failure(raw.error());
        }
        try
        {
            const auto &result = raw.value();
            MarketList list;
            list.total = ji64(result, "total");
            if (result.contains("list") && result["list"].is_array())
            {
                for (const auto &item : result["list"])
                {
                    list.markets.push_back(parse_market(item));
                }
            }
            return Result<MarketList>::success(std::move(list));
        }
        catch (const std::exception &error)
        {
            return Result<MarketList>::failure(make_parse_error(error.what(), "/market", raw.value().dump()));
        }
    }

    Result<Market> ClobClient::get_market(std::int64_t market_id) const
    {
        auto raw = call("GET", "/market/" + std::to_string(market_id), {}, {}, false);
        if (!raw)
        {
            return Result<Market>::failure(raw.error());
        }
        try
        {
            return Result<Market>::success(parse_market_detail(raw.value()));
        }
        catch (const std::exception &error)
        {
            return Result<Market>::failure(make_parse_error(error.what(), "/market/{marketId}", raw.value().dump()));
        }
    }

    Result<Market> ClobClient::get_categorical_market(std::int64_t market_id) const
    {
        auto raw = call("GET", "/market/categorical/" + std::to_string(market_id), {}, {}, false);
        if (!raw)
        {
            return Result<Market>::failure(raw.error());
        }
        try
        {
            return Result<Market>::success(parse_market_detail(raw.value()));
        }
        catch (const std::exception &error)
        {
            return Result<Market>::failure(
                make_parse_error(error.what(), "/market/categorical/{marketId}", raw.value().dump()));
        }
    }

    Result<Market> ClobClient::get_market_by_slug(const std::string &slug) const
    {
        auto raw = call("GET", "/market/slug/" + url_encode(slug), {}, {}, false);
        if (!raw)
        {
            return Result<Market>::failure(raw.error());
        }
        try
        {
            return Result<Market>::success(parse_market_detail(raw.value()));
        }
        catch (const std::exception &error)
        {
            return Result<Market>::failure(make_parse_error(error.what(), "/market/slug/{slug}", raw.value().dump()));
        }
    }

    Result<std::vector<Label>> ClobClient::list_labels() const
    {
        auto raw = call("GET", "/label", {}, {}, false);
        if (!raw)
        {
            return Result<std::vector<Label>>::failure(raw.error());
        }
        try
        {
            const auto &result = raw.value();
            std::vector<Label> labels;
            if (result.contains("list") && result["list"].is_array())
            {
                for (const auto &item : result["list"])
                {
                    Label label;
                    label.label_id = ji64(item, "labelId");
                    label.label_name = jstr(item, "labelName");
                    label.image_url = jstr(item, "imageUrl");
                    label.image_mobile_url = jstr(item, "imageMobileUrl");
                    labels.push_back(std::move(label));
                }
            }
            return Result<std::vector<Label>>::success(std::move(labels));
        }
        catch (const std::exception &error)
        {
            return Result<std::vector<Label>>::failure(make_parse_error(error.what(), "/label", raw.value().dump()));
        }
    }

    Result<Orderbook> ClobClient::get_orderbook(const std::string &token_id) const
    {
        if (token_id.empty())
        {
            return Result<Orderbook>::failure(make_invalid_argument("token_id is required"));
        }
        auto raw = call("GET", "/token/orderbook", {{"token_id", token_id}}, {}, false);
        if (!raw)
        {
            return Result<Orderbook>::failure(raw.error());
        }
        try
        {
            const auto &result = raw.value();
            Orderbook book;
            book.market = jstr(result, "market");
            book.token_id = jstr(result, "tokenId");
            book.timestamp_ms = ji64(result, "timestamp");
            if (result.contains("bids") && result["bids"].is_array())
            {
                for (const auto &level : result["bids"])
                {
                    book.bids.push_back(parse_level(level));
                }
            }
            if (result.contains("asks") && result["asks"].is_array())
            {
                for (const auto &level : result["asks"])
                {
                    book.asks.push_back(parse_level(level));
                }
            }
            return Result<Orderbook>::success(std::move(book));
        }
        catch (const std::exception &error)
        {
            return Result<Orderbook>::failure(make_parse_error(error.what(), "/token/orderbook", raw.value().dump()));
        }
    }

    Result<LatestPrice> ClobClient::get_latest_price(const std::string &token_id) const
    {
        if (token_id.empty())
        {
            return Result<LatestPrice>::failure(make_invalid_argument("token_id is required"));
        }
        auto raw = call("GET", "/token/latest-price", {{"token_id", token_id}}, {}, false);
        if (!raw)
        {
            return Result<LatestPrice>::failure(raw.error());
        }
        try
        {
            const auto &result = raw.value();
            LatestPrice price;
            price.token_id = jstr(result, "tokenId");
            price.price = jstr(result, "price");
            price.side = jstr(result, "side");
            price.size = jstr(result, "size");
            price.timestamp_ms = ji64(result, "timestamp");
            return Result<LatestPrice>::success(std::move(price));
        }
        catch (const std::exception &error)
        {
            return Result<LatestPrice>::failure(
                make_parse_error(error.what(), "/token/latest-price", raw.value().dump()));
        }
    }

    Result<std::vector<PricePoint>> ClobClient::get_price_history(const PriceHistoryQuery &query) const
    {
        if (query.token_id.empty())
        {
            return Result<std::vector<PricePoint>>::failure(make_invalid_argument("token_id is required"));
        }
        std::map<std::string, std::string> params{{"token_id", query.token_id}};
        if (!query.interval.empty())
        {
            params["interval"] = query.interval;
        }
        if (query.start_at)
        {
            params["start_at"] = std::to_string(*query.start_at);
        }
        if (query.end_at)
        {
            params["end_at"] = std::to_string(*query.end_at);
        }
        auto raw = call("GET", "/token/price-history", params, {}, false);
        if (!raw)
        {
            return Result<std::vector<PricePoint>>::failure(raw.error());
        }
        try
        {
            const auto &result = raw.value();
            std::vector<PricePoint> history;
            if (result.contains("history") && result["history"].is_array())
            {
                for (const auto &point : result["history"])
                {
                    history.push_back(PricePoint{ji64(point, "t"), jstr(point, "p")});
                }
            }
            return Result<std::vector<PricePoint>>::success(std::move(history));
        }
        catch (const std::exception &error)
        {
            return Result<std::vector<PricePoint>>::failure(
                make_parse_error(error.what(), "/token/price-history", raw.value().dump()));
        }
    }

    Result<QuoteTokenList> ClobClient::list_quote_tokens(const QuoteTokenQuery &query) const
    {
        std::map<std::string, std::string> params;
        if (query.page)
        {
            params["page"] = std::to_string(*query.page);
        }
        if (query.limit)
        {
            params["limit"] = std::to_string(*query.limit);
        }
        if (query.quote_token_name)
        {
            params["quoteTokenName"] = *query.quote_token_name;
        }
        if (query.chain_id)
        {
            params["chainId"] = *query.chain_id;
        }
        auto raw = call("GET", "/quoteToken", params, {}, false);
        if (!raw)
        {
            return Result<QuoteTokenList>::failure(raw.error());
        }
        try
        {
            const auto &result = raw.value();
            QuoteTokenList list;
            list.total = ji64(result, "total");
            if (result.contains("list") && result["list"].is_array())
            {
                for (const auto &item : result["list"])
                {
                    list.tokens.push_back(parse_quote_token(item));
                }
            }
            return Result<QuoteTokenList>::success(std::move(list));
        }
        catch (const std::exception &error)
        {
            return Result<QuoteTokenList>::failure(make_parse_error(error.what(), "/quoteToken", raw.value().dump()));
        }
    }

    Result<FeeRates> ClobClient::get_fee_rates(const std::string &token_id, const std::string &chain_id) const
    {
        if (token_id.empty())
        {
            return Result<FeeRates>::failure(make_invalid_argument("token_id is required"));
        }
        auto raw = call("GET", "/token/fee-rates", {{"token_id", token_id}, {"chain_id", chain_id}}, {}, true);
        if (!raw)
        {
            return Result<FeeRates>::failure(raw.error());
        }
        try
        {
            const auto &result = raw.value();
            FeeRates rates;
            rates.token_id = jstr(result, "tokenId");
            rates.maker_fee_bps = jstr(result, "makerFeeBps");
            rates.taker_fee_bps = jstr(result, "takerFeeBps");
            return Result<FeeRates>::success(std::move(rates));
        }
        catch (const std::exception &error)
        {
            return Result<FeeRates>::failure(make_parse_error(error.what(), "/token/fee-rates", raw.value().dump()));
        }
    }

    Result<ApiKeyCredential> ClobClient::create_api_key(const OrderSigner &signer) const
    {
        return exchange_api_key(signer, ApiKeyAction::Create, "POST");
    }

    Result<ApiKeyCredential> ClobClient::get_api_key(const OrderSigner &signer) const
    {
        return exchange_api_key(signer, ApiKeyAction::Get, "GET");
    }

    Result<bool> ClobClient::delete_api_key(const OrderSigner &signer) const
    {
        ApiKeyAuthHeaders headers;
        try
        {
            headers = signer.sign_api_key_auth(ApiKeyAction::Delete);
        }
        catch (const std::exception &error)
        {
            return Result<bool>::failure(make_signing_error(error.what()));
        }
        auto raw = call("DELETE", "/auth/api-key", {}, {}, false,
                        {{"OPINION_ADDRESS", headers.opinion_address},
                         {"OPINION_SIGNATURE", headers.opinion_signature},
                         {"OPINION_TIMESTAMP", headers.opinion_timestamp}});
        if (!raw)
        {
            return Result<bool>::failure(raw.error());
        }
        return Result<bool>::success(true);
    }

    Result<ApiKeyCredential> ClobClient::exchange_api_key(const OrderSigner &signer,
                                                          ApiKeyAction action,
                                                          const std::string &method) const
    {
        ApiKeyAuthHeaders headers;
        try
        {
            headers = signer.sign_api_key_auth(action);
        }
        catch (const std::exception &error)
        {
            return Result<ApiKeyCredential>::failure(make_signing_error(error.what()));
        }
        auto raw = call(method, "/auth/api-key", {}, {}, false,
                        {{"OPINION_ADDRESS", headers.opinion_address},
                         {"OPINION_SIGNATURE", headers.opinion_signature},
                         {"OPINION_TIMESTAMP", headers.opinion_timestamp}});
        if (!raw)
        {
            return Result<ApiKeyCredential>::failure(raw.error());
        }
        try
        {
            const auto &result = raw.value();
            ApiKeyCredential credential;
            credential.api_key = jstr(result, "apiKey");
            credential.wallet_address = jstr(result, "walletAddress");
            return Result<ApiKeyCredential>::success(std::move(credential));
        }
        catch (const std::exception &error)
        {
            return Result<ApiKeyCredential>::failure(
                make_parse_error(error.what(), "/auth/api-key", raw.value().dump()));
        }
    }

    Result<OrderList> ClobClient::list_orders(const PageQuery &query) const
    {
        if (const SdkError limit_error = check_limit(query.limit); !limit_error.message.empty())
        {
            return Result<OrderList>::failure(limit_error);
        }
        std::map<std::string, std::string> params;
        add_page(params, query);
        auto raw = call("GET", "/order", params, {}, true);
        if (!raw)
        {
            return Result<OrderList>::failure(raw.error());
        }
        try
        {
            const auto &result = raw.value();
            OrderList list;
            list.total = ji64(result, "total");
            if (result.contains("list") && result["list"].is_array())
            {
                for (const auto &item : result["list"])
                {
                    list.orders.push_back(parse_order(item));
                }
            }
            return Result<OrderList>::success(std::move(list));
        }
        catch (const std::exception &error)
        {
            return Result<OrderList>::failure(make_parse_error(error.what(), "/order", raw.value().dump()));
        }
    }

    Result<OrderRecord> ClobClient::get_order(const std::string &order_id) const
    {
        if (order_id.empty())
        {
            return Result<OrderRecord>::failure(make_invalid_argument("order_id is required"));
        }
        auto raw = call("GET", "/order/" + url_encode(order_id), {}, {}, true);
        if (!raw)
        {
            return Result<OrderRecord>::failure(raw.error());
        }
        try
        {
            const auto &result = raw.value();
            if (result.contains("orderData") && result["orderData"].is_object())
            {
                return Result<OrderRecord>::success(parse_order(result["orderData"]));
            }
            return Result<OrderRecord>::success(parse_order(result));
        }
        catch (const std::exception &error)
        {
            return Result<OrderRecord>::failure(make_parse_error(error.what(), "/order/{orderId}", raw.value().dump()));
        }
    }

    Result<ApiKeyCredential> ClobClient::get_user_auth() const
    {
        auto raw = call("GET", "/user/auth", {}, {}, true);
        if (!raw)
        {
            return Result<ApiKeyCredential>::failure(raw.error());
        }
        try
        {
            const auto &result = raw.value();
            ApiKeyCredential credential;
            credential.api_key = jstr(result, "apiKey");
            credential.wallet_address = jstr(result, "walletAddress");
            return Result<ApiKeyCredential>::success(std::move(credential));
        }
        catch (const std::exception &error)
        {
            return Result<ApiKeyCredential>::failure(make_parse_error(error.what(), "/user/auth", raw.value().dump()));
        }
    }

    Result<PositionList> ClobClient::list_positions(const PageQuery &query) const
    {
        if (const SdkError limit_error = check_limit(query.limit); !limit_error.message.empty())
        {
            return Result<PositionList>::failure(limit_error);
        }
        std::map<std::string, std::string> params;
        add_page(params, query);
        auto raw = call("GET", "/positions", params, {}, true);
        if (!raw)
        {
            return Result<PositionList>::failure(raw.error());
        }
        try
        {
            const auto &result = raw.value();
            PositionList list;
            list.total = ji64(result, "total");
            if (result.contains("list") && result["list"].is_array())
            {
                for (const auto &item : result["list"])
                {
                    list.positions.push_back(parse_position(item));
                }
            }
            return Result<PositionList>::success(std::move(list));
        }
        catch (const std::exception &error)
        {
            return Result<PositionList>::failure(make_parse_error(error.what(), "/positions", raw.value().dump()));
        }
    }

    Result<PositionList> ClobClient::list_positions(const std::string &wallet_address, const PageQuery &query) const
    {
        if (wallet_address.empty())
        {
            return Result<PositionList>::failure(make_invalid_argument("wallet_address is required"));
        }
        if (const SdkError limit_error = check_limit(query.limit); !limit_error.message.empty())
        {
            return Result<PositionList>::failure(limit_error);
        }
        std::map<std::string, std::string> params;
        add_page(params, query);
        auto raw = call("GET", "/positions/user/" + url_encode(wallet_address), params, {}, true);
        if (!raw)
        {
            return Result<PositionList>::failure(raw.error());
        }
        try
        {
            const auto &result = raw.value();
            PositionList list;
            list.total = ji64(result, "total");
            if (result.contains("list") && result["list"].is_array())
            {
                for (const auto &item : result["list"])
                {
                    list.positions.push_back(parse_position(item));
                }
            }
            return Result<PositionList>::success(std::move(list));
        }
        catch (const std::exception &error)
        {
            return Result<PositionList>::failure(
                make_parse_error(error.what(), "/positions/user/{walletAddress}", raw.value().dump()));
        }
    }

    Result<TradeList> ClobClient::list_trades(const PageQuery &query) const
    {
        if (const SdkError limit_error = check_limit(query.limit); !limit_error.message.empty())
        {
            return Result<TradeList>::failure(limit_error);
        }
        std::map<std::string, std::string> params;
        add_page(params, query);
        auto raw = call("GET", "/trade", params, {}, true);
        if (!raw)
        {
            return Result<TradeList>::failure(raw.error());
        }
        try
        {
            const auto &result = raw.value();
            TradeList list;
            list.total = ji64(result, "total");
            if (result.contains("list") && result["list"].is_array())
            {
                for (const auto &item : result["list"])
                {
                    list.trades.push_back(parse_trade(item));
                }
            }
            return Result<TradeList>::success(std::move(list));
        }
        catch (const std::exception &error)
        {
            return Result<TradeList>::failure(make_parse_error(error.what(), "/trade", raw.value().dump()));
        }
    }

    Result<TradeList> ClobClient::list_trades(const std::string &wallet_address, const PageQuery &query) const
    {
        if (wallet_address.empty())
        {
            return Result<TradeList>::failure(make_invalid_argument("wallet_address is required"));
        }
        if (const SdkError limit_error = check_limit(query.limit); !limit_error.message.empty())
        {
            return Result<TradeList>::failure(limit_error);
        }
        std::map<std::string, std::string> params;
        add_page(params, query);
        auto raw = call("GET", "/trade/user/" + url_encode(wallet_address), params, {}, true);
        if (!raw)
        {
            return Result<TradeList>::failure(raw.error());
        }
        try
        {
            const auto &result = raw.value();
            TradeList list;
            list.total = ji64(result, "total");
            if (result.contains("list") && result["list"].is_array())
            {
                for (const auto &item : result["list"])
                {
                    list.trades.push_back(parse_trade(item));
                }
            }
            return Result<TradeList>::success(std::move(list));
        }
        catch (const std::exception &error)
        {
            return Result<TradeList>::failure(
                make_parse_error(error.what(), "/trade/user/{walletAddress}", raw.value().dump()));
        }
    }

    Result<Balance> ClobClient::get_balance(const std::string &chain_id) const
    {
        auto raw = call("GET", "/user/balance", {{"chain_id", chain_id}}, {}, true);
        if (!raw)
        {
            return Result<Balance>::failure(raw.error());
        }
        try
        {
            const auto &result = raw.value();
            Balance balance;
            balance.chain_id = jstr(result, "chainId");
            balance.wallet_address = jstr(result, "walletAddress");
            balance.multi_sign_address = jstr(result, "multiSignAddress");
            if (result.contains("balances") && result["balances"].is_array())
            {
                for (const auto &item : result["balances"])
                {
                    TokenBalance token;
                    token.quote_token = jstr(item, "quoteToken");
                    token.available = jstr(item, "availableBalance");
                    token.frozen = jstr(item, "frozenBalance");
                    token.total = jstr(item, "totalBalance");
                    token.decimals = jint(item, "tokenDecimals");
                    balance.balances.push_back(std::move(token));
                }
            }
            return Result<Balance>::success(std::move(balance));
        }
        catch (const std::exception &error)
        {
            return Result<Balance>::failure(make_parse_error(error.what(), "/user/balance", raw.value().dump()));
        }
    }

    Result<PreparedOrder> ClobClient::prepare_order(const OrderSigner &signer, const PlaceOrderRequest &request) const
    {
        try
        {
            if (request.market_id <= 0)
            {
                return Result<PreparedOrder>::failure(make_invalid_argument("market_id must be positive"));
            }
            if (request.token_id.empty())
            {
                return Result<PreparedOrder>::failure(make_invalid_argument("token_id is required"));
            }
            if (request.maker.empty())
            {
                return Result<PreparedOrder>::failure(make_invalid_argument("maker (Safe / portfolio wallet) is required"));
            }
            if (request.exchange_address.empty())
            {
                return Result<PreparedOrder>::failure(make_invalid_argument("exchange_address is required"));
            }
            if (request.currency_address.empty())
            {
                return Result<PreparedOrder>::failure(make_invalid_argument("currency_address is required"));
            }
            const std::string signer_address = request.signer.empty() ? signer.address() : request.signer;
            const std::string human = human_maker_amount(request);
            std::string maker_amount = scale_decimal(human, request.decimals);
            std::string taker_amount = "0";
            if (request.order_type == OrderType::Limit)
            {
                const PriceFraction price = price_fraction_6(request.price);
                std::string k;
                std::string adjusted_maker;
                if (request.side == OrderSide::Buy)
                {
                    k = div_decimal(maker_amount, price.numerator);
                    adjusted_maker = mul_decimal(k, price.numerator);
                    taker_amount = mul_decimal(k, price.denominator);
                }
                else
                {
                    k = div_decimal(maker_amount, price.denominator);
                    adjusted_maker = mul_decimal(k, price.denominator);
                    taker_amount = mul_decimal(k, price.numerator);
                }
                if (k == "0")
                {
                    return Result<PreparedOrder>::failure(make_invalid_argument("maker amount is too small for this price"));
                }
                maker_amount = std::move(adjusted_maker);
            }
            int signature_type = 0;
            if (request.signature_type)
            {
                signature_type = static_cast<int>(*request.signature_type);
            }
            else if (lower_copy(request.maker) != lower_copy(signer_address))
            {
                signature_type = static_cast<int>(SignatureType::PolyGnosisSafe);
            }
            CtfOrder order;
            order.salt = request.salt ? *request.salt : random_salt();
            order.maker = request.maker;
            order.signer = signer_address;
            order.taker = "0x0000000000000000000000000000000000000000";
            order.token_id = request.token_id;
            order.maker_amount = maker_amount;
            order.taker_amount = taker_amount;
            order.expiration = request.expiration.empty() ? "0" : request.expiration;
            order.nonce = request.nonce.empty() ? "0" : request.nonce;
            order.fee_rate_bps = request.fee_rate_bps.empty() ? "0" : request.fee_rate_bps;
            order.side = static_cast<int>(request.side);
            order.signature_type = signature_type;
            SignedCtfOrder signed_order = signer.sign_order(order, request.exchange_address);
            const auto now = std::chrono::system_clock::now();
            const auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
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
                {"side", std::to_string(signed_order.order.side)},
                {"signatureType", std::to_string(signed_order.order.signature_type)},
                {"signature", signed_order.signature},
                {"sign", signed_order.signature},
                {"contractAddress", ""},
                {"currencyAddress", to_checksum_address(request.currency_address)},
                {"price", request.order_type == OrderType::Market ? "0" : request.price},
                {"tradingMethod", static_cast<int>(request.order_type)},
                {"timestamp", timestamp},
                {"safeRate", "0"},
                {"orderExpTime", "0"},
            };
            if (request.post_only)
            {
                body["postOnly"] = true;
            }
            PreparedOrder prepared;
            prepared.signed_order = std::move(signed_order);
            prepared.body_json = body.dump();
            return Result<PreparedOrder>::success(std::move(prepared));
        }
        catch (const std::exception &error)
        {
            return Result<PreparedOrder>::failure(make_invalid_argument(error.what()));
        }
    }

    Result<MutationResult> ClobClient::place_order(const OrderSigner &signer, PlaceOrderRequest request) const
    {
        if (request.exchange_address.empty() || request.currency_address.empty() || request.decimals <= 0)
        {
            auto market = get_market(request.market_id);
            if (!market)
            {
                market = get_categorical_market(request.market_id);
            }
            if (!market)
            {
                return Result<MutationResult>::failure(market.error());
            }
            auto tokens = list_quote_tokens(QuoteTokenQuery{});
            if (!tokens)
            {
                return Result<MutationResult>::failure(tokens.error());
            }
            const std::string wanted = lower_copy(market.value().quote_token);
            const QuoteToken *match = nullptr;
            for (const auto &token : tokens.value().tokens)
            {
                if (lower_copy(token.address) == wanted)
                {
                    match = &token;
                    break;
                }
            }
            if (!match)
            {
                return Result<MutationResult>::failure(make_invalid_argument("quote token for this market was not in GET /quoteToken"));
            }
            if (request.exchange_address.empty())
            {
                request.exchange_address = match->ctf_exchange_address;
            }
            if (request.currency_address.empty())
            {
                request.currency_address = match->address;
            }
            if (request.decimals <= 0)
            {
                request.decimals = match->decimals;
            }
        }
        auto prepared = prepare_order(signer, request);
        if (!prepared)
        {
            return Result<MutationResult>::failure(prepared.error());
        }
        auto raw = call("POST", "/order", {}, prepared.value().body_json, true);
        if (!raw)
        {
            return Result<MutationResult>::failure(raw.error());
        }
        MutationResult mutation;
        mutation.accepted = true;
        mutation.raw_result = raw.value().dump();
        try
        {
            const auto &result = raw.value();
            if (result.contains("orderData") && result["orderData"].is_object())
            {
                mutation.order_id = jstr(result["orderData"], "orderId");
            }
            else
            {
                mutation.order_id = jstr(result, "orderId");
            }
        }
        catch (const std::exception &)
        {
        }
        return Result<MutationResult>::success(std::move(mutation));
    }

    Result<MutationResult> ClobClient::cancel_order(const std::string &order_id) const
    {
        if (order_id.empty())
        {
            return Result<MutationResult>::failure(make_invalid_argument("order_id is required"));
        }
        const json body = {{"orderId", order_id}};
        auto raw = call("POST", "/order/cancel", {}, body.dump(), true);
        if (!raw)
        {
            return Result<MutationResult>::failure(raw.error());
        }
        MutationResult mutation;
        mutation.accepted = true;
        mutation.order_id = order_id;
        mutation.raw_result = raw.value().dump();
        return Result<MutationResult>::success(std::move(mutation));
    }
} // namespace opinion
