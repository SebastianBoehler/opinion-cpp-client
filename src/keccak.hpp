#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace opinion::crypto
{
    std::array<uint8_t, 32> keccak256(const uint8_t *data, std::size_t len);
    std::array<uint8_t, 32> keccak256(std::string_view data);

    std::string to_hex(const uint8_t *data, std::size_t len, bool prefix);
    std::string to_hex(const std::array<uint8_t, 32> &data, bool prefix);

    bool parse_hex(std::string_view text, uint8_t *out, std::size_t out_len, std::string *error);

    std::array<uint8_t, 32> uint256_from_u64(std::uint64_t value);
    std::array<uint8_t, 32> uint256_from_dec(std::string_view decimal);
    std::array<uint8_t, 32> encode_address(std::string_view address);

    std::string checksum_address(const uint8_t address[20]);
    std::string address_from_uncompressed_pubkey(const uint8_t pubkey64[64]);
} // namespace opinion::crypto
