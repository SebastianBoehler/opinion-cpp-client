#include "opinion/decimal_math.hpp"
#include "opinion/environment.hpp"
#include "opinion/order_signer.hpp"
#include "opinion/websocket_client.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace
{
    int g_failed = 0;

    void expect(bool condition, const std::string &label)
    {
        if (!condition)
        {
            std::cerr << "FAIL " << label << "\n";
            ++g_failed;
        }
    }

    void expect_eq(const std::string &actual, const std::string &want, const std::string &label)
    {
        if (actual != want)
        {
            std::cerr << "FAIL " << label << "\n  got  " << actual << "\n  want " << want << "\n";
            ++g_failed;
        }
    }
} // namespace

int main()
{
    using namespace opinion;

    const auto production = Environment::production();
    expect(production.chain_id == 56, "chain id");
    expect_eq(production.rest_base, "https://openapi.opinion.trade/openapi", "openapi host");
    expect_eq(production.websocket_url, "wss://ws.opinion.trade", "ws host");
    expect_eq(Environment::proxy().rest_base, "https://proxy.opinion.trade:8443/openapi", "proxy host");
    production.validate();

    // Hardhat account 0. Vectors from eth_account.encode_typed_data.
    const char *key = "0xac0974bec39a17e36ba4a6b4d238ff944bacb478cbed5efcae784d7bf4f2ff80";
    OrderSigner signer(key);
    expect_eq(signer.address(), "0xf39Fd6e51aad88F6F4ce6aB8827279cffFb92266", "address");

    const auto headers = signer.sign_api_key_auth("create", "1753690000", 56);
    expect_eq(headers.signature,
              "0xb0536f8815f9c91de2cc1ec11ce739264f61d33807d6561676af4222b1b589976d279806292e39701750f5d7d424c60620106e61fd836422a94780270b0d36241b",
              "api key signature");
    expect_eq(headers.address, signer.address(), "api key address header");
    expect_eq(headers.timestamp, "1753690000", "api key timestamp");

    UnsignedOrder order;
    order.salt = "1";
    order.maker = "0x0000000000000000000000000000000000000001";
    order.signer = signer.address();
    order.taker = "0x0000000000000000000000000000000000000000";
    order.token_id = "42";
    order.maker_amount = "1000";
    order.taker_amount = "2000";
    order.expiration = "0";
    order.nonce = "0";
    order.fee_rate_bps = "0";
    order.side = OrderSide::Buy;
    order.signature_type = SignatureType::PolyGnosisSafe;
    order.verifying_contract = "0x5f45344126d6488025b0b84a3a8189f2487a7246";
    order.chain_id = 56;
    const auto signed_order = signer.sign_order(order);
    expect_eq(signed_order.order_hash,
              "0x07402348d1f4486bbbd72da977a5bfb03a88bc96cd9c1c40d46defbd6ff5ad77",
              "order hash");
    expect_eq(signed_order.signature,
              "0x40bb7263afd474c4d4de76de4ed3b7154b3cbb8d4435f84af0afddb11b87fbde3ff58b3740928edc8eec2089a199e9c1b3b4cf5aadbaddb56105128850c40cf71b",
              "order signature");

    expect_eq(decimal_to_scaled_integer("1.5", 18), "1500000000000000000", "scale 1.5");
    expect_eq(decimal_to_scaled_integer("10", 6), "10000000", "scale 10");
    expect(decimal_at_least_one("1"), "1 >= 1");
    expect(decimal_at_least_one("1.0"), "1.0 >= 1");
    expect(!decimal_at_least_one("0.99"), "0.99 < 1");
    expect(valid_limit_price("0.55"), "0.55 price");
    expect(valid_limit_price("0.01"), "0.01 price");
    expect(valid_limit_price("0.99"), "0.99 price");
    expect(!valid_limit_price("0.009"), "too small");
    expect(!valid_limit_price("0.991"), "too big");
    expect(!valid_limit_price("0.12345"), "too many decimals");

    // 1 quote token (18 decimals) at 0.50 -> maker 1e18, taker 2e18.
    const auto half = limit_order_amounts("1000000000000000000", "0.5", true);
    expect_eq(half.maker_amount, "1000000000000000000", "half maker");
    expect_eq(half.taker_amount, "2000000000000000000", "half taker");

    const auto buy = limit_order_amounts(quote_wei_from_base("10", "0.55", 18), "0.55", true);
    expect(buy.maker_amount != "0" && buy.taker_amount != "0", "0.55 amounts");

    WsSubscription depth{std::string(channel::kDepthDiff), 1274, std::nullopt};
    expect_eq(build_channel_message("SUBSCRIBE", depth),
              "{\"action\":\"SUBSCRIBE\",\"channel\":\"market.depth.diff\",\"marketId\":1274}",
              "depth subscribe");
    WsSubscription categorical{std::string(channel::kLastPrice), std::nullopt, 61};
    expect_eq(build_channel_message("SUBSCRIBE", categorical),
              "{\"action\":\"SUBSCRIBE\",\"channel\":\"market.last.price\",\"rootMarketId\":61}",
              "categorical subscribe");
    expect_eq(build_heartbeat_message(), "{\"action\":\"HEARTBEAT\"}", "heartbeat");
    expect_eq(build_ws_url("wss://ws.opinion.trade", "abc"),
              "wss://ws.opinion.trade?apikey=abc",
              "ws url");

    if (g_failed != 0)
    {
        std::cerr << g_failed << " failed\n";
        return 1;
    }
    std::cout << "ok\n";
    return 0;
}
