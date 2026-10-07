#include "opinion/opinion.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace
{
    int failures = 0;

    void expect(bool condition, const std::string &message)
    {
        if (!condition)
        {
            std::cerr << "FAIL " << message << "\n";
            ++failures;
        }
    }
} // namespace

int main()
{
    const auto openapi = opinion::Environment::bnb_mainnet();
    const auto proxy = opinion::Environment::bnb_mainnet_proxy();
    expect(openapi.chain_id == 56, "chain id");
    expect(openapi.websocket_url == "wss://ws.opinion.trade", "websocket host");
    expect(openapi.url_for("/market") == "https://openapi.opinion.trade/openapi/market", "openapi market url");
    expect(openapi.url_for("/openapi/token/orderbook") == "https://openapi.opinion.trade/openapi/token/orderbook",
           "openapi prefix is not doubled");
    expect(proxy.url_for("/order") == "https://proxy.opinion.trade:8443/openapi/order", "proxy order url");
    expect(proxy.url_for("/openapi/order/cancel") == "https://proxy.opinion.trade:8443/openapi/order/cancel",
           "proxy prefix is not doubled");

    expect(opinion::scale_decimal("1.5", 18) == "1500000000000000000", "scale 1.5 to 18 decimals");
    expect(opinion::div_decimal("10", "3") == "3", "integer division");
    const auto price = opinion::price_fraction_6("0.5");
    expect(price.numerator == "500000" && price.denominator == "1000000", "price fraction");

    const std::string subscribe = opinion::WebSocketClient::subscribe_message(opinion::k_channel_depth_diff, 1274, false);
    expect(subscribe == R"({"action":"SUBSCRIBE","channel":"market.depth.diff","marketId":1274})", "subscribe json");
    const std::string categorical = opinion::WebSocketClient::subscribe_message(opinion::k_channel_last_price, 61, true);
    expect(categorical.find("\"rootMarketId\":61") != std::string::npos, "categorical subscribe");

    opinion::LocalOrderbook book;
    opinion::Orderbook snapshot;
    snapshot.token_id = "token";
    snapshot.bids = {{"0.20", "5"}, {"0.15", "2"}};
    snapshot.asks = {{"0.30", "4"}};
    book.apply_snapshot(snapshot);
    expect(book.best_bid() && book.best_bid()->price == "0.20", "best bid");
    expect(book.best_ask() && book.best_ask()->price == "0.30", "best ask");
    const bool applied = book.apply_depth_message(
        R"({"marketId":2764,"tokenId":"token","outcomeSide":1,"side":"bids","price":"0.20","size":"0","msgType":"market.depth.diff"})");
    expect(applied && book.best_bid() && book.best_bid()->price == "0.15", "zero size removes the level");

    opinion::OrderSigner signer("0xac0974bec39a17e36ba4a6b4d238ff944bacb478cbed5efcae784d7bf4f2ff80");
    expect(signer.address() == "0xf39Fd6e51aad88F6F4ce6aB8827279cffFb92266", "checksum address");
    const auto auth = signer.sign_api_key_auth(opinion::ApiKeyAction::Create, "1753690000");
    expect(auth.digest == "0xba3c477f14a98fe0a650ec704c78257b17e01e56b86dd86a3096b791c272d703", "api key digest");
    expect(auth.opinion_signature ==
               "0xb0536f8815f9c91de2cc1ec11ce739264f61d33807d6561676af4222b1b589976d279806292e39701750f5d7d424c60620106e61fd836422a94780270b0d36241b",
           "api key signature");

    opinion::CtfOrder order;
    order.salt = "1";
    order.maker = signer.address();
    order.signer = signer.address();
    order.taker = "0x0000000000000000000000000000000000000000";
    order.token_id = "42";
    order.maker_amount = "1000000";
    order.taker_amount = "2000000";
    order.expiration = "0";
    order.nonce = "0";
    order.fee_rate_bps = "0";
    order.side = 0;
    order.signature_type = 2;
    const auto signed_order = signer.sign_order(order, "0x0000000000000000000000000000000000000001");
    expect(signed_order.digest == "0x7c208033fcb02a3094a747fa1dd6401fdfcfcb9c56ee862b59141b4400782cbd", "order digest");
    expect(signed_order.signature ==
               "0x18dd137fe9700487750db624295eb8af97e009c63e00a0c2a42988c9274f63530f9568711af3edab7a0339232cae778917c40743aec873bc703820200731dee31b",
           "order signature");

    opinion::ClobClient client(opinion::Environment::bnb_mainnet());
    opinion::PlaceOrderRequest request;
    request.market_id = 123;
    request.token_id = "42";
    request.side = opinion::OrderSide::Buy;
    request.order_type = opinion::OrderType::Limit;
    request.price = "0.5";
    request.maker_amount_in_quote_token = "10";
    request.maker = "0x0000000000000000000000000000000000000002";
    request.signer = signer.address();
    request.exchange_address = "0x0000000000000000000000000000000000000001";
    request.currency_address = "0x55d398326f99059ff775485246999027b3197955";
    request.decimals = 6;
    request.salt = "1";
    request.post_only = true;
    const auto prepared = client.prepare_order(signer, request);
    expect(static_cast<bool>(prepared), prepared ? "prepare" : prepared.error().message);
    if (prepared)
    {
        expect(prepared.value().signed_order.order.maker_amount == "10000000", "maker amount");
        expect(prepared.value().signed_order.order.taker_amount == "20000000", "taker amount");
        expect(prepared.value().signed_order.order.signature_type == 2, "safe signature type when maker differs");
        expect(prepared.value().body_json.find("\"topicId\":123") != std::string::npos, "topicId");
        expect(prepared.value().body_json.find("\"tradingMethod\":2") != std::string::npos, "tradingMethod");
        expect(prepared.value().body_json.find("\"postOnly\":true") != std::string::npos, "postOnly");
    }

    if (failures != 0)
    {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "ok\n";
    return 0;
}
