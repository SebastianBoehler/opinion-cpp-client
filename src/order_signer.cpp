#include "opinion/order_signer.hpp"

#include "keccak.hpp"

#include <secp256k1.h>
#include <secp256k1_recovery.h>

#include <chrono>
#include <cstring>
#include <random>
#include <stdexcept>
#include <vector>

namespace opinion
{
    namespace
    {
        using crypto::encode_address;
        using crypto::keccak256;
        using crypto::to_hex;
        using crypto::uint256_from_dec;
        using crypto::uint256_from_u64;

        void append_word(std::vector<std::uint8_t> &out, const std::array<std::uint8_t, 32> &word)
        {
            out.insert(out.end(), word.begin(), word.end());
        }

        std::array<std::uint8_t, 32> hash_type(std::string_view type_string)
        {
            return keccak256(type_string);
        }

        std::array<std::uint8_t, 32> hash_bytes(const std::vector<std::uint8_t> &bytes)
        {
            return keccak256(bytes.data(), bytes.size());
        }

        std::string sign_hash(const secp256k1_context *ctx,
                              const std::uint8_t key[32],
                              const std::array<std::uint8_t, 32> &hash)
        {
            secp256k1_ecdsa_recoverable_signature sig;
            if (secp256k1_ecdsa_sign_recoverable(ctx, &sig, hash.data(), key, nullptr, nullptr) != 1)
                throw std::runtime_error("secp256k1 recoverable sign failed");
            std::uint8_t compact[64];
            int recid = 0;
            secp256k1_ecdsa_recoverable_signature_serialize_compact(ctx, compact, &recid, &sig);
            std::uint8_t full[65];
            std::memcpy(full, compact, 64);
            full[64] = static_cast<std::uint8_t>(recid + 27);
            return to_hex(full, 65, true);
        }
    } // namespace

    struct OrderSigner::Ctx
    {
        secp256k1_context *secp{nullptr};
        std::uint8_t key[32]{};
        ~Ctx()
        {
            if (secp)
                secp256k1_context_destroy(secp);
        }
    };

    OrderSigner::OrderSigner(std::string private_key_hex)
    {
        ctx_ = std::make_unique<Ctx>();
        ctx_->secp = secp256k1_context_create(SECP256K1_CONTEXT_SIGN);
        if (!ctx_->secp)
            throw std::runtime_error("secp256k1_context_create failed");
        std::string error;
        if (!crypto::parse_hex(private_key_hex, ctx_->key, 32, &error))
            throw std::invalid_argument("private key: " + error);
        if (secp256k1_ec_seckey_verify(ctx_->secp, ctx_->key) != 1)
            throw std::invalid_argument("private key is not a valid secp256k1 scalar");

        secp256k1_pubkey pubkey;
        if (secp256k1_ec_pubkey_create(ctx_->secp, &pubkey, ctx_->key) != 1)
            throw std::runtime_error("failed to derive public key");
        std::uint8_t uncompressed[65];
        std::size_t len = sizeof(uncompressed);
        secp256k1_ec_pubkey_serialize(ctx_->secp, uncompressed, &len, &pubkey, SECP256K1_EC_UNCOMPRESSED);
        if (len != 65 || uncompressed[0] != 0x04)
            throw std::runtime_error("unexpected public key encoding");
        address_ = crypto::address_from_uncompressed_pubkey(uncompressed + 1);
    }

    OrderSigner::~OrderSigner() = default;
    OrderSigner::OrderSigner(OrderSigner &&) noexcept = default;
    OrderSigner &OrderSigner::operator=(OrderSigner &&) noexcept = default;

    ApiKeyHeaders OrderSigner::sign_api_key_auth(std::string action, std::string timestamp, int chain_id) const
    {
        if (action != "create" && action != "get" && action != "delete")
            throw std::invalid_argument("API key action must be create, get, or delete");
        if (timestamp.empty() || timestamp.find_first_not_of("0123456789") != std::string::npos)
            throw std::invalid_argument("API key timestamp must be unix seconds");

        const auto domain_type =
            hash_type("EIP712Domain(string name,string version,uint256 chainId)");
        const auto message_type =
            hash_type("OpinionApiKeyAuth(address walletAddress,string action,string timestamp)");

        std::vector<std::uint8_t> domain;
        append_word(domain, domain_type);
        append_word(domain, keccak256(std::string_view{"Opinion OpenAPI"}));
        append_word(domain, keccak256(std::string_view{"1"}));
        append_word(domain, uint256_from_u64(static_cast<std::uint64_t>(chain_id)));
        const auto domain_separator = hash_bytes(domain);

        std::vector<std::uint8_t> message;
        append_word(message, message_type);
        append_word(message, encode_address(address_));
        append_word(message, keccak256(action));
        append_word(message, keccak256(timestamp));
        const auto struct_hash = hash_bytes(message);

        std::vector<std::uint8_t> digest_input;
        digest_input.push_back(0x19);
        digest_input.push_back(0x01);
        append_word(digest_input, domain_separator);
        append_word(digest_input, struct_hash);
        const auto digest = hash_bytes(digest_input);

        ApiKeyHeaders headers;
        headers.address = address_;
        headers.signature = sign_hash(ctx_->secp, ctx_->key, digest);
        headers.timestamp = std::move(timestamp);
        headers.action = std::move(action);
        return headers;
    }

    SignedOrder OrderSigner::sign_order(const UnsignedOrder &order) const
    {
        if (order.signer.empty())
            throw std::invalid_argument("order signer is required");
        // The key must be the signer. Maker may be a different Safe address.
        if (crypto::encode_address(order.signer) != crypto::encode_address(address_))
            throw std::invalid_argument("order signer does not match the private key address");
        if (order.verifying_contract.empty())
            throw std::invalid_argument("verifying contract (quote token ctfExchangeAddress) is required");

        const auto domain_type = hash_type(
            "EIP712Domain(string name,string version,uint256 chainId,address verifyingContract)");
        const auto message_type = hash_type(
            "Order(uint256 salt,address maker,address signer,address taker,uint256 tokenId,"
            "uint256 makerAmount,uint256 takerAmount,uint256 expiration,uint256 nonce,"
            "uint256 feeRateBps,uint8 side,uint8 signatureType)");

        std::vector<std::uint8_t> domain;
        append_word(domain, domain_type);
        append_word(domain, keccak256(std::string_view{"OPINION CTF Exchange"}));
        append_word(domain, keccak256(std::string_view{"1"}));
        append_word(domain, uint256_from_u64(static_cast<std::uint64_t>(order.chain_id)));
        append_word(domain, encode_address(order.verifying_contract));
        const auto domain_separator = hash_bytes(domain);

        std::vector<std::uint8_t> message;
        append_word(message, message_type);
        append_word(message, uint256_from_dec(order.salt));
        append_word(message, encode_address(order.maker));
        append_word(message, encode_address(order.signer));
        append_word(message, encode_address(order.taker));
        append_word(message, uint256_from_dec(order.token_id));
        append_word(message, uint256_from_dec(order.maker_amount));
        append_word(message, uint256_from_dec(order.taker_amount));
        append_word(message, uint256_from_dec(order.expiration));
        append_word(message, uint256_from_dec(order.nonce));
        append_word(message, uint256_from_dec(order.fee_rate_bps));
        append_word(message, uint256_from_u64(static_cast<std::uint64_t>(order.side)));
        append_word(message, uint256_from_u64(static_cast<std::uint64_t>(order.signature_type)));
        const auto struct_hash = hash_bytes(message);

        std::vector<std::uint8_t> digest_input;
        digest_input.push_back(0x19);
        digest_input.push_back(0x01);
        append_word(digest_input, domain_separator);
        append_word(digest_input, struct_hash);
        const auto digest = hash_bytes(digest_input);

        SignedOrder signed_order;
        signed_order.order = order;
        signed_order.order.signer = address_;
        signed_order.signature = sign_hash(ctx_->secp, ctx_->key, digest);
        signed_order.order_hash = to_hex(digest, true);
        return signed_order;
    }

    std::string OrderSigner::random_salt()
    {
        std::random_device device;
        std::mt19937_64 rng(device());
        const std::uint64_t value = rng() & 0x7fffffffffffffffULL;
        return std::to_string(value == 0 ? 1 : value);
    }
} // namespace opinion
