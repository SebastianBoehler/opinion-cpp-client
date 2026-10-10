#pragma once
#include "opinion/decimal_math.hpp"
#include "opinion/order_signer.hpp"
#include <cstring>
#include <ethash/keccak.hpp>
#include <stdexcept>
#include <vector>
namespace opinion::detail
{
    inline int hex_nibble(char character)
    {
        if (character >= '0' && character <= '9')
        {
            return character - '0';
        }
        if (character >= 'a' && character <= 'f')
        {
            return character - 'a' + 10;
        }
        if (character >= 'A' && character <= 'F')
        {
            return character - 'A' + 10;
        }
        throw std::invalid_argument("invalid hex");
    }

    inline std::vector<std::uint8_t> parse_hex(std::string_view hex)
    {
        if (hex.size() >= 2 && hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X'))
        {
            hex.remove_prefix(2);
        }
        if (hex.size() % 2 != 0)
        {
            throw std::invalid_argument("hex length must be even");
        }
        std::vector<std::uint8_t> out(hex.size() / 2);
        for (std::size_t index = 0; index < out.size(); ++index)
        {
            out[index] = static_cast<std::uint8_t>((hex_nibble(hex[index * 2]) << 4) | hex_nibble(hex[index * 2 + 1]));
        }
        return out;
    }

    inline std::array<std::uint8_t, 32> keccak_bytes(const std::uint8_t *data, std::size_t size)
    {
        const auto hash = ethash::keccak256(data, size);
        std::array<std::uint8_t, 32> out{};
        std::memcpy(out.data(), hash.bytes, 32);
        return out;
    }

    inline std::array<std::uint8_t, 32> word_address(std::string_view address)
    {
        const auto bytes = parse_hex(address);
        if (bytes.size() != 20)
        {
            throw std::invalid_argument("address must be 20 bytes");
        }
        std::array<std::uint8_t, 32> word{};
        std::memcpy(word.data() + 12, bytes.data(), 20);
        return word;
    }

    inline std::array<std::uint8_t, 32> word_uint(std::string_view decimal)
    {
        if (decimal.size() >= 2 && decimal[0] == '0' && (decimal[1] == 'x' || decimal[1] == 'X'))
        {
            const auto bytes = parse_hex(decimal);
            if (bytes.size() > 32)
            {
                throw std::invalid_argument("uint256 hex is too wide");
            }
            std::array<std::uint8_t, 32> word{};
            std::memcpy(word.data() + (32 - bytes.size()), bytes.data(), bytes.size());
            return word;
        }
        return uint256_from_decimal(decimal);
    }

    inline std::array<std::uint8_t, 32> word_string(std::string_view value)
    {
        return keccak256(reinterpret_cast<const std::uint8_t *>(value.data()), value.size());
    }

    inline void append_word(std::vector<std::uint8_t> &out, const std::array<std::uint8_t, 32> &word)
    {
        const std::size_t offset = out.size();
        out.resize(offset + word.size());
        std::memcpy(out.data() + offset, word.data(), word.size());
    }

    inline std::array<std::uint8_t, 32> hash_encoded(const std::vector<std::uint8_t> &encoded)
    {
        return keccak256(encoded.data(), encoded.size());
    }

} // namespace opinion::detail
