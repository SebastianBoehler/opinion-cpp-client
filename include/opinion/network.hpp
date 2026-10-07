#pragma once

#include <string>

namespace opinion
{
    // Optional HTTP(S) or SOCKS proxy. Empty means libcurl's environment
    // variables (https_proxy, all_proxy, no_proxy) stay in effect only when
    // the client was not given an explicit proxy. ClobClient copies
    // Environment::proxy_url onto the HttpClient at construction.
    struct NetworkRoute
    {
        std::string proxy_url;
    };
} // namespace opinion
