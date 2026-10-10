#include "opinion/opinion.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <iostream>
#include <ixwebsocket/IXGetFreePort.h>
#include <ixwebsocket/IXHttpServer.h>
#include <mutex>

int main()
{
    std::mutex mutex;
    std::condition_variable changed;
    bool reading = false;
    bool cancelled = false;
    std::atomic<bool> read_timed_out{false};
    std::atomic<bool> correct_order_request{false};
    std::atomic<int> mode{0};
    const int port = ix::getFreePort();
    ix::HttpServer server(port, "127.0.0.1");
    server.setOnConnectionCallback(
        [&](ix::HttpRequestPtr request, std::shared_ptr<ix::ConnectionState>)
        {
            std::string body;
            if (request->uri.find("/order/cancel") != std::string::npos)
            {
                std::lock_guard<std::mutex> lock(mutex);
                correct_order_request = request->method == "POST" && request->body == R"({"orderId":"order-1"})" &&
                                        request->headers.at("apikey") == "test-key";
                cancelled = true;
                changed.notify_all();
                body = R"({"errno":0,"result":{"accepted":true}})";
            }
            else if (mode == 0)
            {
                std::unique_lock<std::mutex> lock(mutex);
                reading = true;
                changed.notify_all();
                read_timed_out = !changed.wait_for(lock, std::chrono::seconds(5), [&] { return cancelled; });
                body = R"({"errno":0,"result":{"tokenId":"token","bids":[{"price":"0.2","size":"3"}],"asks":[]}})";
            }
            else if (mode == 1)
                body = R"({"code":0,"result":{"tokenId":"token","price":"0.42"}})";
            else if (mode == 2)
                body = R"({"errno":123,"errmsg":"rejected","result":{}})";
            else if (mode == 3)
                body = "broken JSON";
            else
                body = R"({"code":0,"result":{"bids":[{"price":{"bad":1},"size":"3"}]}})";
            return std::make_shared<ix::HttpResponse>(
                200, "OK", ix::HttpErrorCode::Ok, ix::WebSocketHttpHeaders{{"Content-Type", "application/json"}}, body);
        });
    const auto listening = server.listen();
    if (!listening.first)
    {
        std::cerr << listening.second << '\n';
        return 1;
    }
    server.start();
    auto environment = opinion::Environment::bnb_mainnet();
    int failures = 0;
    auto check = [&](bool ok, const char *message)
    {
        if (!ok)
        {
            std::cerr << message << '\n';
            ++failures;
        }
    };
    {
        opinion::ClientConfig config;
        config.api_key = "test-key";
        config.http.timeout_ms = 10000;
        opinion::ClobClient client(environment, config);
        // Test-only redirect of a non-const client after validation. Production still requires HTTPS.
        const_cast<opinion::Environment &>(client.environment()).api_host = "http://127.0.0.1:" + std::to_string(port);
        auto read = std::async(std::launch::async, [&] { return client.get_orderbook("token"); });
        {
            std::unique_lock<std::mutex> lock(mutex);
            check(changed.wait_for(lock, std::chrono::seconds(5), [&] { return reading; }), "read reached server");
        }
        const auto mutation = client.cancel_order("order-1");
        const auto book = read.get();
        check(mutation && mutation.value().raw_result == R"({"accepted":true})", "mutation raw result preserved");
        check(!read_timed_out && correct_order_request,
              "cancellation completes while read is blocked with auth and body");
        check(book && book.value().bids.size() == 1 && book.value().bids[0].size == "3", "typed book parsing");
        mode = 1;
        const auto price = client.get_latest_price("token");
        check(price && price.value().price == "0.42", "code envelope parsing");
        mode = 2;
        const auto rejected = client.get_latest_price("token");
        check(!rejected && rejected.error().api_code == 123, "API error metadata");
        mode = 3;
        const auto invalid = client.get_latest_price("token");
        check(!invalid && !invalid.error().message.empty(), "malformed JSON reports error");
        mode = 4;
        const auto schema = client.get_orderbook("token");
        check(!schema && !schema.error().response_body_excerpt.empty(), "typed schema error keeps diagnostics");
    }
    server.stop();
    return failures ? 1 : 0;
}
