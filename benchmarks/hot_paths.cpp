#include "../src/rest_result.hpp"
#include "opinion/opinion.hpp"
#include <chrono>
#include <cstdint>
#include <iostream>

namespace
{
    volatile std::size_t sink = 0;
    template <typename F> void measure(const char *name, int iterations, F operation)
    {
        for (int i = 0; i < 100; ++i)
            operation();
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < iterations; ++i)
            operation();
        const auto end = std::chrono::steady_clock::now();
        std::cout << name << ": " << std::chrono::duration<double, std::micro>(end - start).count() / iterations
                  << " us/op\n";
    }
} // namespace

int main()
{
    const std::string token = "115792089237316195423570985008687907853269984665640564039457584007913129639935";
    opinion::OrderSigner signer("0xac0974bec39a17e36ba4a6b4d238ff944bacb478cbed5efcae784d7bf4f2ff80");
    opinion::CtfOrder order;
    order.salt = token;
    order.token_id = token;
    order.maker = signer.address();
    order.signer = signer.address();
    order.taker = "0x0000000000000000000000000000000000000000";
    order.maker_amount = "1000000000000000000";
    order.taker_amount = "2000000000000000000";
    order.expiration = order.nonce = order.fee_rate_bps = "0";
    opinion::Orderbook snapshot;
    snapshot.token_id = "token";
    for (int i = 100; i < 300; ++i)
        snapshot.bids.push_back({"0." + std::to_string(i), "100"});
    opinion::LocalOrderbook book;
    book.apply_snapshot(snapshot);
    nlohmann::json result = {{"bids", nlohmann::json::array()}};
    for (int i = 0; i < 100; ++i)
        result["bids"].push_back({{"price", "0.42"}, {"size", "1000000000000000000"}});
    const std::string response = nlohmann::json({{"errno", 0}, {"result", result}}).dump();
    measure("rest_100_levels_old_roundtrip", 2000,
            [&]
            {
                auto envelope = opinion::detail::parse_envelope(response);
                sink = nlohmann::json::parse(envelope.result.dump())["bids"].size();
            });
    measure("rest_100_levels_single_parse", 2000,
            [&] { sink = opinion::detail::parse_envelope(response).result["bids"].size(); });
    measure("uint256_78_digits", 20000, [&] { sink = opinion::uint256_from_decimal(token)[0]; });
    measure("sign_order_long_ids", 5000,
            [&] { sink = signer.sign_order(order, "0x0000000000000000000000000000000000000001").signature.size(); });
    measure("best_bid_200_levels", 20000, [&] { sink = book.best_bid()->price.size(); });
}
