#include "opinion/clob_client.hpp"

#include <iostream>

// Public read against the live OpenAPI host. No API key.
//   GET https://openapi.opinion.trade/openapi/market
//   GET https://openapi.opinion.trade/openapi/token/orderbook
//   GET https://openapi.opinion.trade/openapi/token/latest-price

int main()
{
    using namespace opinion;
    http_global_init();

    ClobClient client(Environment::production());
    MarketQuery query;
    query.limit = 5;
    query.status = "activated";
    query.market_type = 0; // binary, per the current market OpenAPI page

    const auto markets = client.list_markets(query);
    if (!markets)
    {
        std::cerr << "list_markets failed: " << markets.error().message
                  << " http=" << markets.error().http_status << "\n";
        return 1;
    }

    std::cout << "markets total=" << markets.value().total
              << " page=" << markets.value().list.size() << "\n";

    std::string token_id;
    std::int64_t market_id = 0;
    for (const auto &market : markets.value().list)
    {
        std::cout << "  " << market.market_id << " " << market.market_title;
        if (!market.yes_token_id.empty())
            std::cout << " yesTokenId=" << market.yes_token_id.substr(0, 16) << "...";
        std::cout << "\n";
        if (token_id.empty() && !market.yes_token_id.empty())
        {
            token_id = market.yes_token_id;
            market_id = market.market_id;
        }
    }

    if (token_id.empty())
    {
        std::cerr << "no binary market on this page had a yesTokenId\n";
        return 1;
    }

    const auto detail = client.get_market(market_id);
    if (!detail)
    {
        std::cerr << "get_market failed: " << detail.error().message << "\n";
        return 1;
    }
    std::cout << "detail " << detail.value().market_id << " " << detail.value().market_title
              << " quote=" << detail.value().quote_token << "\n";

    const auto book = client.get_orderbook(token_id);
    if (!book)
    {
        std::cerr << "get_orderbook failed: " << book.error().message << "\n";
        return 1;
    }
    std::cout << "orderbook bids=" << book.value().bids.size()
              << " asks=" << book.value().asks.size() << "\n";
    if (!book.value().bids.empty())
        std::cout << "  bid " << book.value().bids.front().price << " x " << book.value().bids.front().size << "\n";
    if (!book.value().asks.empty())
        std::cout << "  ask " << book.value().asks.front().price << " x " << book.value().asks.front().size << "\n";

    const auto price = client.get_latest_price(token_id);
    if (!price)
    {
        std::cerr << "get_latest_price failed: " << price.error().message << "\n";
        return 1;
    }
    std::cout << "latest price=" << price.value().price << " side=" << price.value().side
              << " size=" << price.value().size << "\n";
    return 0;
}
