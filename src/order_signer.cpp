#include "opinion/order_signer.hpp"

#include "opinion/decimal_math.hpp"

#include <ethash/keccak.hpp>
#include <openssl/crypto.h>
#include <secp256k1.h>
#include <secp256k1_recovery.h>

#include <chrono>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <vector>

namespace opinion
{
    namespace
    {
        int hex_nibble(char character)
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

        std::vector<std::uint8_t> parse_hex(std::string_view hex)
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
                out[index] = static_cast<std::uint8_t>((hex_nibble(hex[index * 2]) << 4) |
                                                       hex_nibble(hex[index * 2 + 1]));
            }
            return out;
        }

        std::array<std::uint8_t, 32> keccak_bytes(const std::uint8_t *data, std::size_t size)
        {
            const auto hash = ethash::keccak256(data, size);
            std::array<std::uint8_t, 32> out{};
            std::memcpy(out.data(), hash.bytes, 32);
            return out;
        }

        std::array<std::uint8_t, 32> word_address(std::string_view address)
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

        std::array<std::uint8_t, 32> word_uint(std::string_view decimal)
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

        std::array<std::uint8_t, 32> word_string(std::string_view value)
        {
            return keccak_bytes(reinterpret_cast<const std::uint8_t *>(value.data()), value.size());
        }

        void append_word(std::vector<std::uint8_t> &out, const std::array<std::uint8_t, 32> &word)
        {
            const std::size_t offset = out.size();
            out.resize(offset + word.size());
            std::memcpy(out.data() + offset, word.data(), word.size());
        }

        std::array<std::uint8_t, 32> hash_encoded(const std::vector<std::uint8_t> &encoded)
        {
            return keccak_bytes(encoded.data(), encoded.size());
        }

        std::string lower_hex_address(std::string_view address)
        {
            const auto bytes = parse_hex(address);
            if (bytes.size() != 20)
            {
                throw std::invalid_argument("address must be 20 bytes");
            }
            return to_hex(bytes.data(), bytes.size(), false);
        }
    } // namespace

    std::string to_hex(const std::uint8_t *data, std::size_t size, bool prefix)
    {
        static constexpr char digits[] = "0123456789abcdef";
        std::string hex(prefix ? size * 2 + 2 : size * 2, '0');
        std::size_t offset = 0;
        if (prefix)
        {
            hex[0] = '0';
            hex[1] = 'x';
            offset = 2;
        }
        for (std::size_t index = 0; index < size; ++index)
        {
            hex[offset + index * 2] = digits[data[index] >> 4];
            hex[offset + index * 2 + 1] = digits[data[index] & 0x0f];
        }
        return hex;
    }

    std::array<std::uint8_t, 32> keccak256(const std::uint8_t *data, std::size_t size)
    {
        return keccak_bytes(data, size);
    }

    std::array<std::uint8_t, 32> keccak256(std::string_view data)
    {
        return keccak_bytes(reinterpret_cast<const std::uint8_t *>(data.data()), data.size());
    }

    std::string to_checksum_address(std::string_view address)
    {
        const std::string lower = lower_hex_address(address);
        const auto hash = keccak256(lower);
        std::string out = "0x";
        out.reserve(42);
        for (std::size_t index = 0; index < lower.size(); ++index)
        {
            const unsigned nibble = (hash[index / 2] >> (index % 2 == 0 ? 4 : 0)) & 0x0f;
            char character = lower[index];
            if (character >= 'a' && character <= 'f' && nibble >= 8)
            {
                character = static_cast<char>(character - 'a' + 'A');
            }
            out.push_back(character);
        }
        return out;
    }

    const char *api_key_action_name(ApiKeyAction action)
    {
        switch (action)
        {
        case ApiKeyAction::Create:
            return "create";
        case ApiKeyAction::Get:
            return "get";
        case ApiKeyAction::Delete:
            return "delete";
        }
        return "create";
    }

    struct OrderSigner::Impl
    {
        std::array<std::uint8_t, 32> key{};
        std::string address;
        int chain_id{k_chain_id_bnb_mainnet};
        secp256k1_context *context{nullptr};
        mutable std::mutex mutex;

        Impl() = default;
        ~Impl()
        {
            if (context)
            {
                secp256k1_context_destroy(context);
            }
            OPENSSL_cleanse(key.data(), key.size());
        }

        Impl(const Impl &) = delete;
        Impl &operator=(const Impl &) = delete;
    };

    OrderSigner::OrderSigner(std::string private_key, int chain_id) : impl_(std::make_unique<Impl>())
    {
        if (chain_id != k_chain_id_bnb_mainnet)
        {
            throw std::invalid_argument("Opinion signing uses BNB Chain chain id 56");
        }
        auto bytes = parse_hex(private_key);
        if (bytes.size() != 32)
        {
            throw std::invalid_argument("private key must be 32 bytes");
        }
        impl_->context = secp256k1_context_create(SECP256K1_CONTEXT_SIGN);
        if (!impl_->context)
        {
            throw std::runtime_error("secp256k1_context_create failed");
        }
        std::memcpy(impl_->key.data(), bytes.data(), 32);
        OPENSSL_cleanse(bytes.data(), bytes.size());
        if (!secp256k1_ec_seckey_verify(impl_->context, impl_->key.data()))
        {
            throw std::invalid_argument("private key is not valid for secp256k1");
        }
        secp256k1_pubkey pubkey;
        if (!secp256k1_ec_pubkey_create(impl_->context, &pubkey, impl_->key.data()))
        {
            throw std::runtime_error("failed to derive public key");
        }
        std::uint8_t uncompressed[65];
        std::size_t length = sizeof(uncompressed);
        secp256k1_ec_pubkey_serialize(impl_->context, uncompressed, &length, &pubkey, SECP256K1_EC_UNCOMPRESSED);
        const auto hash = keccak256(uncompressed + 1, 64);
        impl_->address = to_checksum_address(to_hex(hash.data() + 12, 20));
        impl_->chain_id = chain_id;
        OPENSSL_cleanse(private_key.data(), private_key.size());
    }

    OrderSigner::~OrderSigner() = default;
    OrderSigner::OrderSigner(OrderSigner &&) noexcept = default;
    OrderSigner &OrderSigner::operator=(OrderSigner &&) noexcept = default;

    const std::string &OrderSigner::address() const { return impl_->address; }
    int OrderSigner::chain_id() const { return impl_->chain_id; }

    std::string OrderSigner::sign_digest(const std::array<std::uint8_t, 32> &digest) const
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        secp256k1_ecdsa_recoverable_signature signature;
        if (!secp256k1_ecdsa_sign_recoverable(impl_->context, &signature, digest.data(), impl_->key.data(),
                                              nullptr, nullptr))
        {
            throw std::runtime_error("secp256k1 signing failed");
        }
        std::uint8_t compact[64];
        int recovery_id = 0;
        secp256k1_ecdsa_recoverable_signature_serialize_compact(impl_->context, compact, &recovery_id, &signature);
        std::uint8_t encoded[65];
        std::memcpy(encoded, compact, 64);
        encoded[64] = static_cast<std::uint8_t>(recovery_id + 27);
        return to_hex(encoded, sizeof(encoded));
    }

    ApiKeyAuthHeaders OrderSigner::sign_api_key_auth(ApiKeyAction action, std::string_view timestamp) const
    {
        std::string stamp(timestamp);
        if (stamp.empty())
        {
            const auto now = std::chrono::system_clock::now();
            stamp = std::to_string(std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count());
        }
        const std::string action_name = api_key_action_name(action);
        std::vector<std::uint8_t> domain;
        append_word(domain, word_string("EIP712Domain(string name,string version,uint256 chainId)"));
        append_word(domain, word_string(k_api_key_domain_name));
        append_word(domain, word_string(k_api_key_domain_version));
        append_word(domain, word_uint(std::to_string(impl_->chain_id)));
        const auto domain_separator = hash_encoded(domain);

        std::vector<std::uint8_t> message;
        append_word(message, word_string("OpinionApiKeyAuth(address walletAddress,string action,string timestamp)"));
        append_word(message, word_address(impl_->address));
        append_word(message, word_string(action_name));
        append_word(message, word_string(stamp));
        const auto struct_hash = hash_encoded(message);

        std::vector<std::uint8_t> digest_bytes{0x19, 0x01};
        digest_bytes.insert(digest_bytes.end(), domain_separator.begin(), domain_separator.end());
        digest_bytes.insert(digest_bytes.end(), struct_hash.begin(), struct_hash.end());
        const auto digest = hash_encoded(digest_bytes);

        ApiKeyAuthHeaders headers;
        headers.opinion_address = impl_->address;
        headers.opinion_timestamp = std::move(stamp);
        headers.digest = to_hex(digest.data(), digest.size());
        headers.opinion_signature = sign_digest(digest);
        return headers;
    }

    SignedCtfOrder OrderSigner::sign_order(const CtfOrder &order, std::string_view exchange_address) const
    {
        std::vector<std::uint8_t> domain;
        append_word(domain, word_string("EIP712Domain(string name,string version,uint256 chainId,address verifyingContract)"));
        append_word(domain, word_string(k_order_domain_name));
        append_word(domain, word_string(k_order_domain_version));
        append_word(domain, word_uint(std::to_string(impl_->chain_id)));
        append_word(domain, word_address(exchange_address));
        const auto domain_separator = hash_encoded(domain);

        static constexpr const char *k_order_type =
            "Order(uint256 salt,address maker,address signer,address taker,uint256 tokenId,uint256 makerAmount,"
            "uint256 takerAmount,uint256 expiration,uint256 nonce,uint256 feeRateBps,uint8 side,uint8 signatureType)";
        std::vector<std::uint8_t> message;
        append_word(message, word_string(k_order_type));
        append_word(message, word_uint(order.salt));
        append_word(message, word_address(order.maker));
        append_word(message, word_address(order.signer));
        append_word(message, word_address(order.taker));
        append_word(message, word_uint(order.token_id));
        append_word(message, word_uint(order.maker_amount));
        append_word(message, word_uint(order.taker_amount));
        append_word(message, word_uint(order.expiration));
        append_word(message, word_uint(order.nonce));
        append_word(message, word_uint(order.fee_rate_bps));
        append_word(message, word_uint(std::to_string(order.side)));
        append_word(message, word_uint(std::to_string(order.signature_type)));
        const auto struct_hash = hash_encoded(message);

        std::vector<std::uint8_t> digest_bytes{0x19, 0x01};
        digest_bytes.insert(digest_bytes.end(), domain_separator.begin(), domain_separator.end());
        digest_bytes.insert(digest_bytes.end(), struct_hash.begin(), struct_hash.end());
        const auto digest = hash_encoded(digest_bytes);

        SignedCtfOrder signed_order;
        signed_order.order = order;
        signed_order.order.maker = to_checksum_address(order.maker);
        signed_order.order.signer = to_checksum_address(order.signer);
        signed_order.order.taker = to_checksum_address(order.taker);
        signed_order.digest = to_hex(digest.data(), digest.size());
        signed_order.signature = sign_digest(digest);
        return signed_order;
    }
} // namespace opinion
