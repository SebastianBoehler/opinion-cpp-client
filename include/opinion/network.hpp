#pragma once

#include <string>

namespace opinion
{
    // Outbound route for HTTP and WebSocket. proxy_url uses libcurl syntax
    // (http, https, socks5, socks5h). interface_name binds the socket, for
    // example "eth0" or a local address.
    struct NetworkRoute
    {
        std::string proxy_url;
        std::string interface_name;

        bool empty() const { return proxy_url.empty() && interface_name.empty(); }
    };

    // Process-wide fallback used when a client leaves its own route empty.
    // Set this before creating clients. A configured route fails closed.
    void set_default_network_route(const NetworkRoute &route);
    NetworkRoute default_network_route();
} // namespace opinion
