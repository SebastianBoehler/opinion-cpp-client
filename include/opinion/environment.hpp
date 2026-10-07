#pragma once

#include <string>
#include <string_view>

namespace opinion
{
    // Contract addresses published by the TypeScript SDK configuration page and
    // by opinion_clob_sdk 0.7.0 `config.py`. The CTF exchange is not one of
    // these: place-order reads `ctfExchangeAddress` from GET /quoteToken.
    struct Contracts
    {
        std::string conditional_tokens{"0xAD1a38cEc043e70E83a3eC30443dB285ED10D774"};
        std::string multisend{"0x38869bf66a61cF6bDB996A6aE40D5853Fd43B526"};
        std::string fee_manager{"0xC9063Dc52dEEfb518E5b6634A6b8D624bc5d7c36"};
    };

    // One Opinion deployment. `rest_base` always includes the `/openapi` prefix,
    // so client paths are `/market` and `/order` on both presets.
    //
    // production() uses the TypeScript default host
    //   https://openapi.opinion.trade/openapi
    // proxy() uses the Python / community Go host with that same prefix appended
    //   https://proxy.opinion.trade:8443/openapi
    // Passing the Python host string without `/openapi` will 404.
    struct Environment
    {
        std::string name;
        int chain_id{56};
        std::string rest_base;
        std::string websocket_url;
        std::string rpc_url;
        std::string proxy_url;
        Contracts contracts;

        static Environment production();
        static Environment proxy();
        // "production" or "proxy".
        static Environment from_name(std::string_view name);

        void validate() const;
    };

    inline constexpr int kBnbChainId = 56;
    inline constexpr const char *kOpenApiHost = "https://openapi.opinion.trade/openapi";
    inline constexpr const char *kProxyHostWithPrefix = "https://proxy.opinion.trade:8443/openapi";
    inline constexpr const char *kWebsocketUrl = "wss://ws.opinion.trade";
} // namespace opinion
