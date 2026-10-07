#include "opinion/decimal_math.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

namespace opinion
{
    namespace
    {
        class Big
        {
        public:
            static Big zero() { return Big(); }

            static Big from_u64(std::uint64_t v)
            {
                Big b;
                if (v == 0)
                    return b;
                while (v > 0)
                {
                    b.limbs_.push_back(static_cast<std::uint32_t>(v & 0xffffffffu));
                    v >>= 32;
                }
                return b;
            }

            static Big from_dec(std::string_view text)
            {
                Big b;
                for (char c : text)
                {
                    if (c < '0' || c > '9')
                        throw std::invalid_argument("decimal integer expected");
                    b = b.mul_small(10).add_small(static_cast<std::uint32_t>(c - '0'));
                }
                return b;
            }

            bool is_zero() const { return limbs_.empty(); }

            int cmp(const Big &other) const
            {
                if (limbs_.size() != other.limbs_.size())
                    return limbs_.size() < other.limbs_.size() ? -1 : 1;
                for (std::size_t i = limbs_.size(); i-- > 0;)
                {
                    if (limbs_[i] != other.limbs_[i])
                        return limbs_[i] < other.limbs_[i] ? -1 : 1;
                }
                return 0;
            }

            Big add(const Big &other) const
            {
                Big out;
                const std::size_t n = std::max(limbs_.size(), other.limbs_.size());
                out.limbs_.resize(n);
                std::uint64_t carry = 0;
                for (std::size_t i = 0; i < n; ++i)
                {
                    const std::uint64_t sum = carry + limb(i) + other.limb(i);
                    out.limbs_[i] = static_cast<std::uint32_t>(sum & 0xffffffffu);
                    carry = sum >> 32;
                }
                if (carry)
                    out.limbs_.push_back(static_cast<std::uint32_t>(carry));
                return out;
            }

            Big add_small(std::uint32_t v) const { return add(from_u64(v)); }

            Big mul_small(std::uint32_t v) const
            {
                if (v == 0 || is_zero())
                    return zero();
                Big out;
                out.limbs_.resize(limbs_.size() + 1);
                std::uint64_t carry = 0;
                for (std::size_t i = 0; i < limbs_.size(); ++i)
                {
                    const std::uint64_t prod =
                        carry + static_cast<std::uint64_t>(limbs_[i]) * v;
                    out.limbs_[i] = static_cast<std::uint32_t>(prod & 0xffffffffu);
                    carry = prod >> 32;
                }
                if (carry)
                    out.limbs_.back() = static_cast<std::uint32_t>(carry);
                out.trim();
                return out;
            }

            Big mul(const Big &other) const
            {
                if (is_zero() || other.is_zero())
                    return zero();
                Big out;
                out.limbs_.assign(limbs_.size() + other.limbs_.size(), 0);
                for (std::size_t i = 0; i < limbs_.size(); ++i)
                {
                    std::uint64_t carry = 0;
                    for (std::size_t j = 0; j < other.limbs_.size(); ++j)
                    {
                        const std::uint64_t prod = carry + out.limbs_[i + j] +
                                                   static_cast<std::uint64_t>(limbs_[i]) * other.limbs_[j];
                        out.limbs_[i + j] = static_cast<std::uint32_t>(prod & 0xffffffffu);
                        carry = prod >> 32;
                    }
                    out.limbs_[i + other.limbs_.size()] = static_cast<std::uint32_t>(carry);
                }
                out.trim();
                return out;
            }

            std::pair<Big, std::uint32_t> divmod_small(std::uint32_t v) const
            {
                if (v == 0)
                    throw std::invalid_argument("division by zero");
                Big q;
                q.limbs_.resize(limbs_.size());
                std::uint64_t rem = 0;
                for (std::size_t i = limbs_.size(); i-- > 0;)
                {
                    const std::uint64_t cur = (rem << 32) | limbs_[i];
                    q.limbs_[i] = static_cast<std::uint32_t>(cur / v);
                    rem = cur % v;
                }
                q.trim();
                return {q, static_cast<std::uint32_t>(rem)};
            }

            std::pair<Big, Big> divmod(const Big &d) const
            {
                if (d.is_zero())
                    throw std::invalid_argument("division by zero");
                if (cmp(d) < 0)
                    return {zero(), *this};
                // Long division in base 10 is fine at order-amount sizes.
                const std::string num = to_dec();
                const std::string den = d.to_dec();
                std::string q;
                Big rem;
                for (char c : num)
                {
                    rem = rem.mul_small(10).add_small(static_cast<std::uint32_t>(c - '0'));
                    std::uint32_t digit = 0;
                    while (rem.cmp(d) >= 0)
                    {
                        rem = rem.sub(d);
                        ++digit;
                    }
                    if (!q.empty() || digit != 0)
                        q.push_back(static_cast<char>('0' + digit));
                }
                if (q.empty())
                    q = "0";
                return {from_dec(q), rem};
            }

            Big sub(const Big &other) const
            {
                if (cmp(other) < 0)
                    throw std::invalid_argument("negative unsigned result");
                Big out;
                out.limbs_.resize(limbs_.size());
                std::int64_t borrow = 0;
                for (std::size_t i = 0; i < limbs_.size(); ++i)
                {
                    const std::int64_t diff = static_cast<std::int64_t>(limb(i)) -
                                              static_cast<std::int64_t>(other.limb(i)) - borrow;
                    if (diff < 0)
                    {
                        out.limbs_[i] = static_cast<std::uint32_t>(diff + (std::int64_t{1} << 32));
                        borrow = 1;
                    }
                    else
                    {
                        out.limbs_[i] = static_cast<std::uint32_t>(diff);
                        borrow = 0;
                    }
                }
                out.trim();
                return out;
            }

            std::string to_dec() const
            {
                if (is_zero())
                    return "0";
                Big n = *this;
                std::string out;
                while (!n.is_zero())
                {
                    auto [q, r] = n.divmod_small(10);
                    out.push_back(static_cast<char>('0' + r));
                    n = q;
                }
                std::reverse(out.begin(), out.end());
                return out;
            }

            static Big pow10(int n)
            {
                Big b = from_u64(1);
                for (int i = 0; i < n; ++i)
                    b = b.mul_small(10);
                return b;
            }

            static Big gcd(Big a, Big b)
            {
                while (!b.is_zero())
                {
                    auto [q, r] = a.divmod(b);
                    (void)q;
                    a = std::move(b);
                    b = std::move(r);
                }
                return a;
            }

        private:
            std::vector<std::uint32_t> limbs_;

            std::uint32_t limb(std::size_t i) const
            {
                return i < limbs_.size() ? limbs_[i] : 0;
            }

            void trim()
            {
                while (!limbs_.empty() && limbs_.back() == 0)
                    limbs_.pop_back();
            }
        };

        std::string strip(std::string_view text)
        {
            std::size_t b = 0;
            while (b < text.size() && std::isspace(static_cast<unsigned char>(text[b])))
                ++b;
            std::size_t e = text.size();
            while (e > b && std::isspace(static_cast<unsigned char>(text[e - 1])))
                --e;
            return std::string(text.substr(b, e - b));
        }

        struct ParsedDecimal
        {
            std::string digits; // integer digits of value * 10^scale, no dot
            int scale{0};
        };

        ParsedDecimal parse_decimal(std::string_view raw)
        {
            const std::string text = strip(raw);
            if (text.empty() || text[0] == '-' || text[0] == '+')
                throw std::invalid_argument("amount must be a positive decimal");
            const auto dot = text.find('.');
            if (text.find('.', dot == std::string::npos ? 0 : dot + 1) != std::string::npos && dot != std::string::npos)
            {
                if (std::count(text.begin(), text.end(), '.') != 1)
                    throw std::invalid_argument("amount has more than one decimal point");
            }
            std::string whole;
            std::string frac;
            if (dot == std::string::npos)
                whole = text;
            else
            {
                whole = text.substr(0, dot);
                frac = text.substr(dot + 1);
            }
            if (whole.empty())
                whole = "0";
            if (whole.find_first_not_of("0123456789") != std::string::npos ||
                frac.find_first_not_of("0123456789") != std::string::npos)
                throw std::invalid_argument("amount is not a decimal number");
            ParsedDecimal parsed;
            parsed.scale = static_cast<int>(frac.size());
            parsed.digits = whole + frac;
            auto nz = parsed.digits.find_first_not_of('0');
            if (nz == std::string::npos)
                parsed.digits = "0";
            else
                parsed.digits.erase(0, nz);
            return parsed;
        }

        std::pair<Big, Big> price_fraction(std::string_view price)
        {
            const auto parsed = parse_decimal(price);
            if (parsed.digits == "0")
                throw std::invalid_argument("price must be positive");
            Big num = Big::from_dec(parsed.digits);
            Big den = Big::pow10(parsed.scale);
            const Big g = Big::gcd(num, den);
            if (!g.is_zero() && g.cmp(Big::from_u64(1)) != 0)
            {
                num = num.divmod(g).first;
                den = den.divmod(g).first;
            }
            return {num, den};
        }

        std::string round_significant(const std::string &digits, int n)
        {
            if (digits == "0")
                return "0";
            if (static_cast<int>(digits.size()) <= n)
                return digits;
            const int drop = static_cast<int>(digits.size()) - n;
            std::string head = digits.substr(0, static_cast<std::size_t>(n));
            const int next = digits[static_cast<std::size_t>(n)] - '0';
            bool rest = false;
            for (std::size_t i = static_cast<std::size_t>(n + 1); i < digits.size(); ++i)
            {
                if (digits[i] != '0')
                    rest = true;
            }
            bool up = false;
            if (next > 5 || (next == 5 && rest))
                up = true;
            else if (next == 5 && !rest)
                up = ((head.back() - '0') % 2) == 1;

            if (up)
            {
                int carry = 1;
                for (int i = static_cast<int>(head.size()) - 1; i >= 0 && carry; --i)
                {
                    const int d = (head[static_cast<std::size_t>(i)] - '0') + carry;
                    head[static_cast<std::size_t>(i)] = static_cast<char>('0' + (d % 10));
                    carry = d / 10;
                }
                if (carry)
                {
                    head.insert(head.begin(), '1');
                    // One extra digit means one fewer trailing zero so the magnitude stays put.
                    if (drop > 0)
                        return head + std::string(static_cast<std::size_t>(drop - 1), '0');
                    return head;
                }
            }
            return head + std::string(static_cast<std::size_t>(drop), '0');
        }
    } // namespace

    std::string decimal_to_scaled_integer(std::string_view amount, int decimals)
    {
        if (decimals < 0 || decimals > 18)
            throw std::invalid_argument("decimals must be between 0 and 18");
        const auto parsed = parse_decimal(amount);
        if (parsed.digits == "0")
            return "0";
        std::string digits = parsed.digits;
        if (parsed.scale < decimals)
            digits.append(static_cast<std::size_t>(decimals - parsed.scale), '0');
        else if (parsed.scale > decimals)
            digits.erase(digits.size() - static_cast<std::size_t>(parsed.scale - decimals));
        if (digits.empty())
            return "0";
        auto nz = digits.find_first_not_of('0');
        if (nz == std::string::npos)
            return "0";
        return digits.substr(nz);
    }

    bool decimal_at_least_one(std::string_view amount)
    {
        const auto parsed = parse_decimal(amount);
        if (parsed.digits == "0")
            return false;
        // value >= 1 iff there is at least one digit to the left of the decimal point
        // that is non-zero. digits holds (whole+frac) without leading zeros, scale is frac length.
        return static_cast<int>(parsed.digits.size()) > parsed.scale;
    }

    bool valid_limit_price(std::string_view price, std::string *reason)
    {
        try
        {
            const std::string text = strip(price);
            const auto dot = text.find('.');
            if (dot != std::string::npos && text.size() - dot - 1 > 4)
            {
                if (reason)
                    *reason = "price allows at most 4 decimal places";
                return false;
            }
            const std::string scaled = decimal_to_scaled_integer(text, 4);
            const Big value = Big::from_dec(scaled);
            // 0.01 -> 100, 0.99 -> 9900, at scale 4.
            if (value.cmp(Big::from_u64(100)) < 0 || value.cmp(Big::from_u64(9900)) > 0)
            {
                if (reason)
                    *reason = "price must be between 0.01 and 0.99";
                return false;
            }
            return true;
        }
        catch (const std::exception &ex)
        {
            if (reason)
                *reason = ex.what();
            return false;
        }
    }

    LimitAmounts limit_order_amounts(std::string_view maker_wei, std::string_view price, bool buy)
    {
        std::string reason;
        if (!valid_limit_price(price, &reason))
            throw std::invalid_argument(reason);
        const auto [num, den] = price_fraction(price);
        const std::string rounded = round_significant(std::string(maker_wei), 4);
        const Big target = Big::from_dec(rounded);
        const Big step = buy ? num : den;
        Big k = target.divmod(step).first;
        if (k.is_zero())
            k = Big::from_u64(1);
        Big maker = buy ? k.mul(num) : k.mul(den);
        Big taker = buy ? k.mul(den) : k.mul(num);
        if (maker.is_zero())
            maker = Big::from_u64(1);
        if (taker.is_zero())
            taker = Big::from_u64(1);
        return LimitAmounts{maker.to_dec(), taker.to_dec()};
    }

    std::string quote_wei_from_base(std::string_view base_amount, std::string_view price, int decimals)
    {
        // floor(base * price * 10^decimals)
        const auto parsed = parse_decimal(base_amount);
        const auto [num, den] = price_fraction(price);
        Big base = Big::from_dec(parsed.digits);
        base = base.mul(Big::pow10(decimals));
        if (parsed.scale > 0)
            base = base.divmod(Big::pow10(parsed.scale)).first;
        return base.mul(num).divmod(den).first.to_dec();
    }

    std::string base_wei_from_quote(std::string_view quote_amount, std::string_view price, int decimals)
    {
        // floor(quote / price * 10^decimals)
        const auto parsed = parse_decimal(quote_amount);
        const auto [num, den] = price_fraction(price);
        Big quote = Big::from_dec(parsed.digits);
        quote = quote.mul(Big::pow10(decimals)).mul(den);
        if (parsed.scale > 0)
            quote = quote.divmod(Big::pow10(parsed.scale)).first;
        return quote.divmod(num).first.to_dec();
    }
} // namespace opinion
