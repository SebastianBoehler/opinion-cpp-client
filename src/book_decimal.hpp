#pragma once
#include <algorithm>
#include <stdexcept>
#include <string_view>

namespace opinion::detail
{
    struct DecimalView
    {
        std::string_view whole;
        std::string_view fraction;
    };
    inline DecimalView decimal_view(std::string_view text)
    {
        if (!text.empty() && text.front() == '+')
            text.remove_prefix(1);
        bool digit = false;
        bool dot = false;
        for (const char character : text)
        {
            if (character == '.' && !dot)
            {
                dot = true;
                continue;
            }
            if (character < '0' || character > '9')
                throw std::invalid_argument("book value must be a non-negative decimal");
            digit = true;
        }
        if (!digit)
            throw std::invalid_argument("book value is empty");
        const auto point = text.find('.');
        auto whole = text.substr(0, point);
        const auto fraction = point == text.npos ? std::string_view{} : text.substr(point + 1);
        while (!whole.empty() && whole.front() == '0')
            whole.remove_prefix(1);
        return {whole, fraction};
    }
    inline int compare_prices(std::string_view left, std::string_view right)
    {
        const auto a = decimal_view(left);
        const auto b = decimal_view(right);
        if (a.whole.size() != b.whole.size())
            return a.whole.size() < b.whole.size() ? -1 : 1;
        if (a.whole != b.whole)
            return a.whole < b.whole ? -1 : 1;
        for (std::size_t i = 0; i < std::max(a.fraction.size(), b.fraction.size()); ++i)
        {
            const char x = i < a.fraction.size() ? a.fraction[i] : '0';
            const char y = i < b.fraction.size() ? b.fraction[i] : '0';
            if (x != y)
                return x < y ? -1 : 1;
        }
        return 0;
    }
    inline bool is_zero_size(std::string_view size)
    {
        const auto value = decimal_view(size);
        return value.whole.empty() &&
               std::all_of(value.fraction.begin(), value.fraction.end(), [](char digit) { return digit == '0'; });
    }
} // namespace opinion::detail
