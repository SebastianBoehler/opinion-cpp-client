#include "opinion/opinion.hpp"

#include <iostream>

int main()
{
    opinion::ClobClient client(opinion::Environment::bnb_mainnet());
    opinion::MarketListQuery query;
    query.limit = 5;
    query.status = "activated";
    query.sort_by = 5;
    query.market_type = 2;

    const auto markets = client.list_markets(query);
    if (!markets)
    {
        std::cerr << "GET /market failed: " << markets.error().message << "\n";
        return 1;
    }
    std::cout << "markets total=" << markets.value().total
              << " page=" << markets.value().markets.size() << "\n";
    if (markets.value().markets.empty())
    {
        std::cerr << "market list was empty\n";
        return 1;
    }

    const auto &market = markets.value().markets.front();
    std::cout << "market " << market.market_id << " chain " << market.chain_id
              << " \"" << market.market_title << "\"\n";
    std::string token_id = market.yes_token_id;
    if (token_id.empty() && !market.child_markets.empty())
    {
        token_id = market.child_markets.front().yes_token_id;
    }
    if (token_id.empty())
    {
        std::cerr << "market has no yes token id\n";
        return 1;
    }

    const auto book = client.get_orderbook(token_id);
    if (!book)
    {
        std::cerr << "GET /token/orderbook failed: " << book.error().message << "\n";
        return 1;
    }
    std::cout << "orderbook token=" << book.value().token_id
              << " bids=" << book.value().bids.size()
              << " asks=" << book.value().asks.size() << "\n";
    if (!book.value().bids.empty())
    {
        std::cout << "best bid " << book.value().bids.front().price
                  << " x " << book.value().bids.front().size << "\n";
    }
    if (!book.value().asks.empty())
    {
        std::cout << "best ask " << book.value().asks.front().price
                  << " x " << book.value().asks.front().size << "\n";
    }

    const auto quotes = client.list_quote_tokens({});
    if (!quotes)
    {
        std::cerr << "GET /quoteToken failed: " << quotes.error().message << "\n";
        return 1;
    }
    for (const auto &token : quotes.value().tokens)
    {
        std::cout << "quote " << token.symbol << " decimals=" << token.decimals
                  << " exchange=" << token.ctf_exchange_address << "\n";
    }
    return quotes.value().tokens.empty() ? 1 : 0;
}
