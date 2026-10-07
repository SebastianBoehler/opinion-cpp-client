#pragma once

#include <string>
#include <string_view>

namespace opinion
{
    // Decimal strings stay exact. No floating point.
    // `decimals` is the quote-token `decimal` field from GET /quoteToken
    // (USDT on BNB Chain is 18 in the live quote-token list, not 6).

    // "1.5" with decimals=18 -> "1500000000000000000". Extra fractional digits
    // beyond `decimals` are truncated toward zero.
    std::string decimal_to_scaled_integer(std::string_view amount, int decimals);

    // True when the human-readable decimal is >= 1.
    bool decimal_at_least_one(std::string_view amount);

    // Documented limit-order price band: 0.01 through 0.99, at most 4 decimal places.
    bool valid_limit_price(std::string_view price, std::string *reason = nullptr);

    struct LimitAmounts
    {
        std::string maker_amount;
        std::string taker_amount;
    };

    // Matches opinion_clob_sdk 0.7.0 `calculate_order_amounts`:
    // round the wei maker amount to 4 significant digits, then scale so
    // maker/taker (BUY) or taker/maker (SELL) equals the price fraction exactly.
    // Integer half-to-even rounding is used for the significant-digit step.
    LimitAmounts limit_order_amounts(std::string_view maker_wei, std::string_view price, bool buy);

    // Human base * price, or human quote / price, then scale by 10^decimals.
    std::string quote_wei_from_base(std::string_view base_amount, std::string_view price, int decimals);
    std::string base_wei_from_quote(std::string_view quote_amount, std::string_view price, int decimals);
} // namespace opinion
