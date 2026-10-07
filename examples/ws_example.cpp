#include "opinion/opinion.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>

int main()
{
    const opinion::Environment environment = opinion::Environment::bnb_mainnet();
    const char *api_key = std::getenv("OPINION_API_KEY");
    const std::string subscribe = opinion::WebSocketClient::subscribe_message(
        opinion::k_channel_depth_diff, 1274, false);
    if (!api_key || api_key[0] == '\0')
    {
        std::cout << "websocket " << environment.websocket_url << "?apikey=<API_KEY>\n"
                  << "subscribe " << subscribe << "\n"
                  << "heartbeat {\"action\":\"HEARTBEAT\"}\n"
                  << "Set OPINION_API_KEY to open the socket.\n";
        return 0;
    }

    opinion::WebSocketClient socket;
    socket.set_url(environment.websocket_url_with_key(api_key));
    socket.on_message([](const std::string &message) { std::cout << message << "\n"; });
    socket.on_error([](const std::string &error) { std::cerr << "websocket error: " << error << "\n"; });
    if (!socket.connect() || !socket.wait_until_connected(std::chrono::seconds(10)))
    {
        std::cerr << "websocket connect failed\n";
        return 1;
    }
    socket.subscribe(subscribe);
    socket.send_heartbeat();
    socket.disconnect();
    return 0;
}
