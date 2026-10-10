#include "opinion/decimal_math.hpp"
#include <stdexcept>

namespace opinion
{
    std::array<std::uint8_t, 32> uint256_from_decimal(std::string_view decimal)
    {
        if (decimal.empty())
            throw std::invalid_argument("decimal integer is empty");
        std::array<std::uint8_t, 32> out{};
        for (const char digit : decimal)
        {
            if (digit < '0' || digit > '9')
                throw std::invalid_argument("decimal integer contains a non-digit");
            unsigned carry = static_cast<unsigned>(digit - '0');
            for (auto byte = out.rbegin(); byte != out.rend(); ++byte)
            {
                const unsigned next = static_cast<unsigned>(*byte) * 10 + carry;
                *byte = static_cast<std::uint8_t>(next & 0xff);
                carry = next >> 8;
            }
            if (carry)
                throw std::invalid_argument("integer does not fit in uint256");
        }
        return out;
    }
} // namespace opinion
