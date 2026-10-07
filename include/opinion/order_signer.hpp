#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace opinion
{
    // EIP-712 side used on the signed Order. This is not the REST order-list
    // side (1 = buy, 2 = sell). Official SDKs use 0 = BUY, 1 = SELL.
    enum class OrderSide
    {
        Buy = 0,
        Sell = 1
    };

    // tradingMethod on POST /order: 1 = market, 2 = limit.
    enum class OrderType
    {
        Market = 1,
        Limit = 2
    };

    // Opinion's Python and Go clients send signatureType 2 (POLY_GNOSIS_SAFE).
    // maker is the Safe / multiSig that holds funds; signer is the EOA.
    enum class SignatureType
    {
        Eoa = 0,
        PolyProxy = 1,
        PolyGnosisSafe = 2
    };

    struct ApiKeyHeaders
    {
        std::string address;    // OPINION_ADDRESS
        std::string signature;  // OPINION_SIGNATURE
        std::string timestamp;  // OPINION_TIMESTAMP, unix seconds, matches the signed field
        std::string action;     // create, get, or delete
    };

    // Fields of the CTF Order typed data. Amounts, salt, token id, and fee are
    // decimal integer strings (wei / raw units), not human-readable decimals.
    struct UnsignedOrder
    {
        std::string salt;
        std::string maker;
        std::string signer;
        std::string taker{"0x0000000000000000000000000000000000000000"};
        std::string token_id;
        std::string maker_amount;
        std::string taker_amount;
        std::string expiration{"0"};
        std::string nonce{"0"};
        std::string fee_rate_bps{"0"};
        OrderSide side{OrderSide::Buy};
        SignatureType signature_type{SignatureType::PolyGnosisSafe};
        std::string verifying_contract;
        int chain_id{56};
    };

    struct SignedOrder
    {
        UnsignedOrder order;
        std::string signature; // 0x + 65-byte recoverable ECDSA, v = 27 or 28
        std::string order_hash;
    };

    // secp256k1 + Keccak-256 signer for API-key minting and CTF orders.
    //
    // API key typed data (docs: Authentication):
    //   domain  name="Opinion OpenAPI", version="1", chainId=56
    //           (no verifyingContract)
    //   type    OpinionApiKeyAuth(address walletAddress,string action,string timestamp)
    //   action  "create" | "get" | "delete"
    //
    // Order typed data (opinion_clob_sdk 0.7.0 Order EIP712Struct and
    // opinion-go-order-utils):
    //   domain  name="OPINION CTF Exchange", version="1", chainId, verifyingContract
    //   type    Order(uint256 salt,address maker,address signer,address taker,
    //                uint256 tokenId,uint256 makerAmount,uint256 takerAmount,
    //                uint256 expiration,uint256 nonce,uint256 feeRateBps,
    //                uint8 side,uint8 signatureType)
    class OrderSigner
    {
    public:
        explicit OrderSigner(std::string private_key_hex);
        ~OrderSigner();

        OrderSigner(const OrderSigner &) = delete;
        OrderSigner &operator=(const OrderSigner &) = delete;
        OrderSigner(OrderSigner &&) noexcept;
        OrderSigner &operator=(OrderSigner &&) noexcept;

        // EIP-55 checksum of the key's address.
        const std::string &address() const { return address_; }

        ApiKeyHeaders sign_api_key_auth(std::string action, std::string timestamp, int chain_id = 56) const;
        SignedOrder sign_order(const UnsignedOrder &order) const;

        static std::string random_salt();

    private:
        struct Ctx;
        std::unique_ptr<Ctx> ctx_;
        std::string address_;
    };
} // namespace opinion
