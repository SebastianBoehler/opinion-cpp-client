#include "opinion/websocket_client.hpp"

#include <cstdlib>
#include <iostream>
#include <thread>

// Prints the market-channel frames this client sends.
// Connects only when OPINION_API_KEY is set, because the socket URL requires apikey.

int main(int argc, char **argv)
{
    using namespace opinion;
    const std::int64_t market_id = argc > 1 ? std::strtoll(argv[1], nullptr, 10) : 1274;

    WsSubscription depth{std::string(channel::kDepthDiff), market_id, std::nullopt};
    WsSubscription price{std::string(channel::kLastPrice), market_id, std::nullopt};
    std::cout << build_ws_url(kWebsocketUrl, "YOUR_API_KEY") << "\n";
    std::cout << build_heartbeat_message() << "\n";
    std::cout << build_channel_message("SUBSCRIBE", depth) << "\n";
    std::cout << build_channel_message("SUBSCRIBE", price) << "\n";

    const char *api_key = std::getenv("OPINION_API_KEY");
    if (!api_key || !*api_key)
    {
        std::cout << "OPINION_API_KEY unset; not connecting\n";
        return 0;
    }

    MarketStream stream(Environment::production(), api_key);
    stream.socket().set_reconnect(false);
    stream.socket().set_message_callback([](const WsMessage &message) {
        std::cout << message.raw << "\n";
    });
    stream.socket().set_error_callback([](const std::string &error) {
        std::cerr << "ws error: " << error << "\n";
    });
    stream.connect();
    stream.subscribe_depth(market_id);
    stream.subscribe_last_price(market_id);
    std::this_thread::sleep_for(std::chrono::seconds(5));
    stream.close();
    return 0;
}
