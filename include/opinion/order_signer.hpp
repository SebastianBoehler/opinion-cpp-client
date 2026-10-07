#pragma once

#include "opinion/environment.hpp"
#include "opinion/types.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace opinion
{
    struct ApiKeyAuthHeaders
    {
        std::string opinion_address;
        std::string opinion_signature;
        std::string opinion_timestamp;
        std::string digest;
    };

    // secp256k1 signer for Opinion EIP-712 payloads.
    //
    // API keys use typed data OpinionApiKeyAuth over domain
    // ("Opinion OpenAPI", "1", chainId 56) with no verifyingContract.
    //
    // Orders use the CTF Order struct over domain
    // ("OPINION CTF Exchange", "1", chainId, verifyingContract = quote token exchange).
    // The published SDK signs the EIP-712 digest with raw ECDSA. Builder docs also
    // mention personal_sign of a struct hash; that is not what OrderBuilder.sign does.
    class OrderSigner
    {
    public:
        explicit OrderSigner(std::string private_key, int chain_id = k_chain_id_bnb_mainnet);
        ~OrderSigner();

        OrderSigner(const OrderSigner &) = delete;
        OrderSigner &operator=(const OrderSigner &) = delete;
        OrderSigner(OrderSigner &&) noexcept;
        OrderSigner &operator=(OrderSigner &&) noexcept;

        const std::string &address() const;
        int chain_id() const;

        ApiKeyAuthHeaders sign_api_key_auth(ApiKeyAction action, std::string_view timestamp = {}) const;
        SignedCtfOrder sign_order(const CtfOrder &order, std::string_view exchange_address) const;

        std::string sign_digest(const std::array<std::uint8_t, 32> &digest) const;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

    std::string to_hex(const std::uint8_t *data, std::size_t size, bool prefix = true);
    std::string to_checksum_address(std::string_view address);
    std::array<std::uint8_t, 32> keccak256(const std::uint8_t *data, std::size_t size);
    std::array<std::uint8_t, 32> keccak256(std::string_view data);
    const char *api_key_action_name(ApiKeyAction action);
} // namespace opinion
