#include "opinion/decimal_math.hpp"

#include <algorithm>
#include <stdexcept>
#include <vector>

namespace opinion
{
    namespace
    {
        std::string strip_zeros(std::string digits)
        {
            std::size_t index = 0;
            while (index + 1 < digits.size() && digits[index] == '0')
            {
                ++index;
            }
            digits.erase(0, index);
            return digits.empty() ? "0" : digits;
        }

        void require_digits(std::string_view value)
        {
            if (value.empty())
            {
                throw std::invalid_argument("decimal integer is empty");
            }
            for (char character : value)
            {
                if (character < '0' || character > '9')
                {
                    throw std::invalid_argument("decimal integer contains a non-digit");
                }
            }
        }

        int compare_digits(std::string_view left_view, std::string_view right_view)
        {
            const std::string left = strip_zeros(std::string(left_view));
            const std::string right = strip_zeros(std::string(right_view));
            if (left.size() != right.size())
            {
                return left.size() < right.size() ? -1 : 1;
            }
            if (left == right)
            {
                return 0;
            }
            return left < right ? -1 : 1;
        }

        std::string add_digits(std::string_view left, std::string_view right)
        {
            std::string result;
            int carry = 0;
            int i = static_cast<int>(left.size()) - 1;
            int j = static_cast<int>(right.size()) - 1;
            while (i >= 0 || j >= 0 || carry)
            {
                int sum = carry;
                if (i >= 0)
                {
                    sum += left[static_cast<std::size_t>(i--)] - '0';
                }
                if (j >= 0)
                {
                    sum += right[static_cast<std::size_t>(j--)] - '0';
                }
                result.push_back(static_cast<char>('0' + (sum % 10)));
                carry = sum / 10;
            }
            std::reverse(result.begin(), result.end());
            return strip_zeros(result);
        }

        std::string sub_digits(std::string_view left, std::string_view right)
        {
            if (compare_digits(left, right) < 0)
            {
                throw std::invalid_argument("negative unsigned subtraction");
            }
            std::string result;
            int borrow = 0;
            int i = static_cast<int>(left.size()) - 1;
            int j = static_cast<int>(right.size()) - 1;
            while (i >= 0)
            {
                int digit = left[static_cast<std::size_t>(i--)] - '0' - borrow;
                if (j >= 0)
                {
                    digit -= right[static_cast<std::size_t>(j--)] - '0';
                }
                if (digit < 0)
                {
                    digit += 10;
                    borrow = 1;
                }
                else
                {
                    borrow = 0;
                }
                result.push_back(static_cast<char>('0' + digit));
            }
            std::reverse(result.begin(), result.end());
            return strip_zeros(result);
        }

        std::string mul_small(std::string_view left, int factor)
        {
            if (factor < 0 || factor > 9)
            {
                throw std::invalid_argument("mul_small factor out of range");
            }
            if (factor == 0 || left == "0")
            {
                return "0";
            }
            std::string result;
            int carry = 0;
            for (int index = static_cast<int>(left.size()) - 1; index >= 0; --index)
            {
                const int product = (left[static_cast<std::size_t>(index)] - '0') * factor + carry;
                result.push_back(static_cast<char>('0' + (product % 10)));
                carry = product / 10;
            }
            while (carry)
            {
                result.push_back(static_cast<char>('0' + (carry % 10)));
                carry /= 10;
            }
            std::reverse(result.begin(), result.end());
            return result;
        }
    } // namespace

    int compare_decimal(std::string_view left, std::string_view right)
    {
        require_digits(left);
        require_digits(right);
        return compare_digits(left, right);
    }

    std::string mul_decimal(std::string_view left, std::string_view right)
    {
        require_digits(left);
        require_digits(right);
        const std::string left_digits = strip_zeros(std::string(left));
        const std::string right_digits = strip_zeros(std::string(right));
        left = left_digits;
        right = right_digits;
        if (left == "0" || right == "0")
        {
            return "0";
        }
        std::vector<int> digits(left.size() + right.size(), 0);
        for (int i = static_cast<int>(left.size()) - 1; i >= 0; --i)
        {
            for (int j = static_cast<int>(right.size()) - 1; j >= 0; --j)
            {
                const int index = i + j + 1;
                digits[static_cast<std::size_t>(index)] += (left[static_cast<std::size_t>(i)] - '0') *
                                                           (right[static_cast<std::size_t>(j)] - '0');
                digits[static_cast<std::size_t>(index - 1)] += digits[static_cast<std::size_t>(index)] / 10;
                digits[static_cast<std::size_t>(index)] %= 10;
            }
        }
        std::string result;
        for (int digit : digits)
        {
            if (result.empty() && digit == 0)
            {
                continue;
            }
            result.push_back(static_cast<char>('0' + digit));
        }
        return result.empty() ? "0" : result;
    }

    std::string div_decimal(std::string_view numerator, std::string_view denominator)
    {
        require_digits(numerator);
        require_digits(denominator);
        const std::string numerator_digits = strip_zeros(std::string(numerator));
        const std::string denominator_digits = strip_zeros(std::string(denominator));
        numerator = numerator_digits;
        denominator = denominator_digits;
        if (denominator == "0")
        {
            throw std::invalid_argument("division by zero");
        }
        if (compare_digits(numerator, denominator) < 0)
        {
            return "0";
        }
        std::string quotient;
        std::string remainder = "0";
        for (char character : numerator)
        {
            if (remainder == "0")
            {
                remainder.clear();
            }
            remainder.push_back(character);
            remainder = strip_zeros(remainder);
            int digit = 0;
            if (compare_digits(remainder, denominator) >= 0)
            {
                for (int candidate = 9; candidate >= 1; --candidate)
                {
                    const std::string product = mul_small(denominator, candidate);
                    if (compare_digits(product, remainder) <= 0)
                    {
                        digit = candidate;
                        remainder = sub_digits(remainder, product);
                        break;
                    }
                }
            }
            quotient.push_back(static_cast<char>('0' + digit));
        }
        return strip_zeros(quotient);
    }

    std::string scale_decimal(std::string_view amount, int decimals)
    {
        if (decimals < 0 || decimals > 18)
        {
            throw std::invalid_argument("decimals must be between 0 and 18");
        }
        std::string text(amount);
        if (!text.empty() && text.front() == '+')
        {
            text.erase(text.begin());
        }
        const auto dot = text.find('.');
        std::string whole = dot == std::string::npos ? text : text.substr(0, dot);
        std::string fraction = dot == std::string::npos ? std::string() : text.substr(dot + 1);
        if (whole.empty())
        {
            whole = "0";
        }
        require_digits(whole);
        require_digits(fraction.empty() ? "0" : std::string_view(fraction));
        if (static_cast<int>(fraction.size()) > decimals)
        {
            throw std::invalid_argument("amount has more fractional digits than token decimals");
        }
        fraction.append(static_cast<std::size_t>(decimals) - fraction.size(), '0');
        return strip_zeros(whole + fraction);
    }

    std::string uint256_to_decimal(const std::array<std::uint8_t, 32> &be)
    {
        auto multiply_by = [](std::string_view digits, int factor)
        {
            std::string product;
            int carry = 0;
            for (int index = static_cast<int>(digits.size()) - 1; index >= 0; --index)
            {
                const int value = (digits[static_cast<std::size_t>(index)] - '0') * factor + carry;
                product.push_back(static_cast<char>('0' + (value % 10)));
                carry = value / 10;
            }
            while (carry)
            {
                product.push_back(static_cast<char>('0' + (carry % 10)));
                carry /= 10;
            }
            std::reverse(product.begin(), product.end());
            return strip_zeros(product);
        };

        std::string digits = "0";
        for (std::uint8_t byte : be)
        {
            digits = add_digits(multiply_by(digits, 256), std::to_string(byte));
        }
        return digits;
    }

    PriceFraction price_fraction_6(std::string_view price)
    {
        std::string text(price);
        if (!text.empty() && text.front() == '+')
        {
            text.erase(text.begin());
        }
        if (text.empty() || text.front() == '-')
        {
            throw std::invalid_argument("price must be greater than 0 and less than 1");
        }
        const auto dot = text.find('.');
        const std::string whole = dot == std::string::npos ? text : text.substr(0, dot);
        std::string fraction = dot == std::string::npos ? std::string() : text.substr(dot + 1);
        if (whole.empty() || fraction.find('.') != std::string::npos)
        {
            throw std::invalid_argument("price must be a decimal in (0, 1)");
        }
        for (char character : whole + fraction)
        {
            if (character < '0' || character > '9')
            {
                throw std::invalid_argument("price must be a decimal in (0, 1)");
            }
        }
        if (fraction.size() > 6)
        {
            throw std::invalid_argument("price supports at most 6 decimal places");
        }
        bool whole_zero = true;
        for (char character : whole)
        {
            if (character != '0')
            {
                whole_zero = false;
            }
        }
        if (!whole_zero)
        {
            throw std::invalid_argument("price must be greater than 0 and less than 1");
        }
        fraction.append(6 - fraction.size(), '0');
        bool frac_zero = true;
        for (char character : fraction)
        {
            if (character != '0')
            {
                frac_zero = false;
            }
        }
        if (frac_zero)
        {
            throw std::invalid_argument("price must be greater than 0 and less than 1");
        }
        PriceFraction out;
        std::size_t index = 0;
        while (index + 1 < fraction.size() && fraction[index] == '0')
        {
            ++index;
        }
        out.numerator = fraction.substr(index);
        out.denominator = "1000000";
        return out;
    }
} // namespace opinion
