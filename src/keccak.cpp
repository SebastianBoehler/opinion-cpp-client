#include "keccak.hpp"

#include <cstring>
#include <stdexcept>

namespace opinion::crypto
{
    namespace
    {
        constexpr std::uint64_t rotl64(std::uint64_t x, int n)
        {
            return (x << n) | (x >> (64 - n));
        }

        void keccak_f(std::uint64_t st[25])
        {
            static constexpr std::uint64_t rc[24] = {
                0x0000000000000001ULL, 0x0000000000008082ULL, 0x800000000000808aULL,
                0x8000000080008000ULL, 0x000000000000808bULL, 0x0000000080000001ULL,
                0x8000000080008081ULL, 0x8000000000008009ULL, 0x000000000000008aULL,
                0x0000000000000088ULL, 0x0000000080008009ULL, 0x000000008000000aULL,
                0x000000008000808bULL, 0x800000000000008bULL, 0x8000000000008089ULL,
                0x8000000000008003ULL, 0x8000000000008002ULL, 0x8000000000000080ULL,
                0x000000000000800aULL, 0x800000008000000aULL, 0x8000000080008081ULL,
                0x8000000000008080ULL, 0x0000000080000001ULL, 0x8000000080008008ULL};
            static constexpr int rotc[24] = {1,  3,  6,  10, 15, 21, 28, 36, 45, 55, 2,  14,
                                             27, 41, 56, 8,  25, 43, 62, 18, 39, 61, 20, 44};
            static constexpr int piln[24] = {10, 7,  11, 17, 18, 3, 5,  16, 8,  21, 24, 4,
                                             15, 23, 19, 13, 12, 2, 20, 14, 22, 9,  6,  1};

            for (int round = 0; round < 24; ++round)
            {
                std::uint64_t bc[5];
                for (int i = 0; i < 5; ++i)
                    bc[i] = st[i] ^ st[i + 5] ^ st[i + 10] ^ st[i + 15] ^ st[i + 20];

                for (int i = 0; i < 5; ++i)
                {
                    const std::uint64_t t = bc[(i + 4) % 5] ^ rotl64(bc[(i + 1) % 5], 1);
                    for (int j = 0; j < 25; j += 5)
                        st[j + i] ^= t;
                }

                std::uint64_t t = st[1];
                for (int i = 0; i < 24; ++i)
                {
                    const int j = piln[i];
                    bc[0] = st[j];
                    st[j] = rotl64(t, rotc[i]);
                    t = bc[0];
                }

                for (int j = 0; j < 25; j += 5)
                {
                    std::uint64_t tmp[5];
                    for (int i = 0; i < 5; ++i)
                        tmp[i] = st[j + i];
                    for (int i = 0; i < 5; ++i)
                        st[j + i] ^= (~tmp[(i + 1) % 5]) & tmp[(i + 2) % 5];
                }

                st[0] ^= rc[round];
            }
        }

        std::uint64_t load_le64(const std::uint8_t *p)
        {
            std::uint64_t v = 0;
            for (int i = 0; i < 8; ++i)
                v |= static_cast<std::uint64_t>(p[i]) << (8 * i);
            return v;
        }

        void store_le64(std::uint8_t *p, std::uint64_t v)
        {
            for (int i = 0; i < 8; ++i)
                p[i] = static_cast<std::uint8_t>(v >> (8 * i));
        }
    } // namespace

    std::array<uint8_t, 32> keccak256(const uint8_t *data, std::size_t len)
    {
        constexpr std::size_t rate = 136;
        std::uint64_t st[25] = {};
        std::uint8_t block[rate];

        while (len >= rate)
        {
            for (std::size_t i = 0; i < rate / 8; ++i)
                st[i] ^= load_le64(data + i * 8);
            keccak_f(st);
            data += rate;
            len -= rate;
        }

        std::memset(block, 0, rate);
        if (len > 0)
            std::memcpy(block, data, len);
        block[len] ^= 0x01;
        block[rate - 1] ^= 0x80;
        for (std::size_t i = 0; i < rate / 8; ++i)
            st[i] ^= load_le64(block + i * 8);
        keccak_f(st);

        std::array<uint8_t, 32> out{};
        for (int i = 0; i < 4; ++i)
            store_le64(out.data() + i * 8, st[i]);
        return out;
    }

    std::array<uint8_t, 32> keccak256(std::string_view data)
    {
        return keccak256(reinterpret_cast<const uint8_t *>(data.data()), data.size());
    }

    std::string to_hex(const uint8_t *data, std::size_t len, bool prefix)
    {
        static constexpr char kHex[] = "0123456789abcdef";
        std::string out;
        out.reserve((prefix ? 2 : 0) + len * 2);
        if (prefix)
            out.append("0x");
        for (std::size_t i = 0; i < len; ++i)
        {
            out.push_back(kHex[data[i] >> 4]);
            out.push_back(kHex[data[i] & 0x0f]);
        }
        return out;
    }

    std::string to_hex(const std::array<uint8_t, 32> &data, bool prefix)
    {
        return to_hex(data.data(), data.size(), prefix);
    }

    bool parse_hex(std::string_view text, uint8_t *out, std::size_t out_len, std::string *error)
    {
        if (text.size() >= 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
            text.remove_prefix(2);
        if (text.size() != out_len * 2)
        {
            if (error)
                *error = "hex length mismatch";
            return false;
        }
        auto nibble = [](char c) -> int {
            if (c >= '0' && c <= '9')
                return c - '0';
            if (c >= 'a' && c <= 'f')
                return c - 'a' + 10;
            if (c >= 'A' && c <= 'F')
                return c - 'A' + 10;
            return -1;
        };
        for (std::size_t i = 0; i < out_len; ++i)
        {
            const int hi = nibble(text[i * 2]);
            const int lo = nibble(text[i * 2 + 1]);
            if (hi < 0 || lo < 0)
            {
                if (error)
                    *error = "invalid hex digit";
                return false;
            }
            out[i] = static_cast<uint8_t>((hi << 4) | lo);
        }
        return true;
    }

    std::array<uint8_t, 32> uint256_from_u64(std::uint64_t value)
    {
        std::array<uint8_t, 32> out{};
        for (int i = 0; i < 8; ++i)
            out[31 - i] = static_cast<uint8_t>(value >> (8 * i));
        return out;
    }

    std::array<uint8_t, 32> uint256_from_dec(std::string_view decimal)
    {
        if (decimal.empty())
            throw std::invalid_argument("empty uint256");
        std::array<uint8_t, 32> out{};
        for (char c : decimal)
        {
            if (c < '0' || c > '9')
                throw std::invalid_argument("uint256 is not a decimal integer");
            unsigned carry = static_cast<unsigned>(c - '0');
            for (int i = 31; i >= 0; --i)
            {
                const unsigned v = static_cast<unsigned>(out[static_cast<std::size_t>(i)]) * 10u + carry;
                out[static_cast<std::size_t>(i)] = static_cast<uint8_t>(v & 0xffu);
                carry = v >> 8;
            }
            if (carry != 0)
                throw std::invalid_argument("uint256 overflow");
        }
        return out;
    }

    std::array<uint8_t, 32> encode_address(std::string_view address)
    {
        uint8_t raw[20];
        std::string error;
        if (!parse_hex(address, raw, sizeof(raw), &error))
            throw std::invalid_argument("address: " + error);
        std::array<uint8_t, 32> out{};
        std::memcpy(out.data() + 12, raw, 20);
        return out;
    }

    std::string checksum_address(const uint8_t address[20])
    {
        const std::string hex = to_hex(address, 20, false);
        const auto hash = keccak256(hex);
        std::string out = "0x";
        out.reserve(42);
        for (int i = 0; i < 40; ++i)
        {
            char c = hex[static_cast<std::size_t>(i)];
            const unsigned nibble =
                (hash[static_cast<std::size_t>(i / 2)] >> ((i % 2 == 0) ? 4 : 0)) & 0x0f;
            if (c >= 'a' && nibble >= 8)
                c = static_cast<char>(c - 32);
            out.push_back(c);
        }
        return out;
    }

    std::string address_from_uncompressed_pubkey(const uint8_t pubkey64[64])
    {
        const auto hash = keccak256(pubkey64, 64);
        return checksum_address(hash.data() + 12);
    }
} // namespace opinion::crypto
