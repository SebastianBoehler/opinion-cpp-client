#include "opinion/opinion.hpp"
#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
    int failures = 0;
    void check(bool ok, const char *message)
    {
        if (!ok)
        {
            std::cerr << message << '\n';
            ++failures;
        }
    }
    template <typename F> void rejects(F operation)
    {
        bool rejected = false;
        try
        {
            operation();
        }
        catch (const std::invalid_argument &)
        {
            rejected = true;
        }
        check(rejected, "expected invalid_argument");
    }
} // namespace
int main()
{
    const std::string maximum = "115792089237316195423570985008687907853269984665640564039457584007913129639935";
    std::array<std::uint8_t, 32> expected;
    expected.fill(255);
    check(opinion::uint256_from_decimal(maximum) == expected, "uint256 max");
    check(opinion::uint256_from_decimal("000" + maximum) == expected, "uint256 leading zeros");
    check(opinion::uint256_to_decimal(expected) == maximum, "uint256 roundtrip");
    rejects(
        []
        {
            opinion::uint256_from_decimal(
                "115792089237316195423570985008687907853269984665640564039457584007913129639936");
        });
    for (const auto *invalid : {"", "-1", "+1", "1x", " 1"})
        rejects([&] { opinion::uint256_from_decimal(invalid); });
    opinion::LocalOrderbook book;
    opinion::Orderbook snapshot;
    snapshot.token_id = "token";
    snapshot.bids = {{"0.15", "2"}, {"0.20", "5"}};
    snapshot.asks = {{"0.3", "4"}, {"0.25", "2"}};
    book.apply_snapshot(snapshot);
    check(book.best_bid()->price == "0.20" && book.best_ask()->price == "0.25", "sorted snapshot");
    book.apply_depth_message(
        R"({"msgType":"market.depth.diff","tokenId":"token","side":"bids","price":"0.2","size":"7"})");
    check(book.book().bids.size() == 2 && book.best_bid()->size == "7", "equivalent price updates one level");
    book.apply_depth_message(
        R"({"msgType":"market.depth.diff","tokenId":"token","side":"bids","price":"00.2000","size":"0.000"})");
    check(book.book().bids.size() == 1 && book.best_bid()->price == "0.15", "equivalent price removes level");
    snapshot.bids = {{"0.2", "1"}, {"0.20", "2"}};
    rejects([&] { book.apply_snapshot(snapshot); });
    check(book.best_bid()->price == "0.15", "rejected snapshot is atomic");
    for (const auto *invalid : {"", "nan", "-1", "1e-2", "1.2.3", "+"})
    {
        snapshot.bids = {{invalid, "1"}};
        rejects([&] { book.apply_snapshot(snapshot); });
    }
    opinion::LocalOrderbook empty;
    rejects(
        [&]
        {
            empty.apply_depth_message(
                R"({"msgType":"market.depth.diff","tokenId":"new","side":"bids","price":"broken","size":"1"})");
        });
    check(empty.book().token_id.empty(), "invalid update must not initialize token");
    std::string error;
    opinion::WebSocketClient socket;
    socket.set_url("ws://127.0.0.1:1");
    opinion::WebSocketOptions options;
    options.proxy_url = "socks5://127.0.0.1:1";
    socket.configure(options);
    socket.on_error([&](const std::string &reason) { error = reason; });
    check(!socket.connect() && !error.empty(), "unsupported proxy must fail closed");
    check(socket.state() == opinion::WsState::Disconnected, "route rejection leaves socket disconnected");
    options.proxy_url.clear();
    options.interface_name = "lo0";
    socket.configure(options);
    error.clear();
    check(!socket.connect() && !error.empty(), "unsupported interface must fail closed");
    opinion::set_default_network_route({"http://127.0.0.1:1", ""});
    socket.configure({});
    error.clear();
    check(!socket.connect() && !error.empty(), "default route must fail closed");
    opinion::set_default_network_route({});
    return failures ? 1 : 0;
}
