#include "opinion/environment.hpp"

#include <stdexcept>

namespace opinion
{
    namespace
    {
        bool has_scheme(const std::string &url, std::string_view scheme)
        {
            return url.size() > scheme.size() && url.compare(0, scheme.size(), scheme) == 0 &&
                   url.find("://") != std::string::npos;
        }
    } // namespace

    Environment Environment::production()
    {
        Environment env;
        env.name = "production";
        env.chain_id = kBnbChainId;
        env.rest_base = kOpenApiHost;
        env.websocket_url = kWebsocketUrl;
        env.rpc_url = "https://bsc-dataseed.binance.org";
        return env;
    }

    Environment Environment::proxy()
    {
        Environment env = production();
        env.name = "proxy";
        // Python examples and the community Go client pass the proxy host
        // without `/openapi` and then request `/openapi/...`. This preset
        // appends the prefix so the same relative paths work as production().
        env.rest_base = kProxyHostWithPrefix;
        return env;
    }

    Environment Environment::from_name(std::string_view name)
    {
        if (name == "production")
            return production();
        if (name == "proxy")
            return proxy();
        throw std::invalid_argument("unknown Opinion environment \"" + std::string(name) +
                                    "\"; expected production or proxy");
    }

    void Environment::validate() const
    {
        if (name.empty())
            throw std::invalid_argument("Environment.name must be non-empty");
        if (chain_id != kBnbChainId)
            throw std::invalid_argument("Opinion documents BNB Chain mainnet only (chain id 56)");
        if (!has_scheme(rest_base, "https://") && !has_scheme(rest_base, "http://"))
            throw std::invalid_argument("Environment.rest_base must be an http(s) URL that already includes /openapi");
        if (rest_base.find("/openapi") == std::string::npos)
            throw std::invalid_argument(
                "Environment.rest_base must include /openapi (openapi host already has it; "
                "proxy.opinion.trade:8443 needs the prefix appended)");
        if (!has_scheme(websocket_url, "wss://") && !has_scheme(websocket_url, "ws://"))
            throw std::invalid_argument("Environment.websocket_url must be a ws(s) URL");
        if (!rpc_url.empty() && !has_scheme(rpc_url, "https://") && !has_scheme(rpc_url, "http://"))
            throw std::invalid_argument("Environment.rpc_url must be an http(s) URL");
    }
} // namespace opinion
