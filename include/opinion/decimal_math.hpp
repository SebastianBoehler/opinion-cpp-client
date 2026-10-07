#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace opinion
{
    // Unsigned decimal integers and fixed-point scaling. Amounts are strings so
    // 18-decimal BSC USDT values are not forced through double or uint64_t.
    std::string scale_decimal(std::string_view amount, int decimals);
    std::string mul_decimal(std::string_view left, std::string_view right);
    std::string div_decimal(std::string_view numerator, std::string_view denominator);
    int compare_decimal(std::string_view left, std::string_view right);

    std::array<std::uint8_t, 32> uint256_from_decimal(std::string_view decimal);
    std::string uint256_to_decimal(const std::array<std::uint8_t, 32> &be);

    // Price in (0, 1) quantized to 6 decimal places, matching the official
    // order builder (numerator / 1_000_000).
    struct PriceFraction
    {
        std::string numerator;
        std::string denominator{"1000000"};
    };

    PriceFraction price_fraction_6(std::string_view price);
} // namespace opinion
