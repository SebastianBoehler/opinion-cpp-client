#pragma once

#include <string>
#include <string_view>

namespace opinion
{
    // Contract addresses published for BNB Chain mainnet (chain id 56).
    // The CTF exchange is not in this table: read ctfExchangeAddress from
    // GET /quoteToken. It differs per quote token.
    struct Contracts
    {
        std::string conditional_tokens{"0xAD1a38cEc043e70E83a3eC30443dB285ED10D774"};
        std::string multisend{"0x38869bf66a61cF6bDB996A6aE40D5853Fd43B526"};
        std::string fee_manager{"0xC9063Dc52dEEfb518E5b6634A6b8D624bc5d7c36"};
    };

    // One Opinion deployment.
    //
    // Host gotcha: the TypeScript SDK default host already ends in `/openapi`
    // (`https://openapi.opinion.trade/openapi`). The community Go client and
    // some Python examples use `https://proxy.opinion.trade:8443` and then
    // prefix every path with `/openapi`. Passing a logical path such as
    // `/market` to Environment::url_for() does the right thing for both.
    // Doubling the prefix (`/openapi/openapi/market`) is a 404.
    struct Environment
    {
        std::string name;
        int chain_id{56};
        std::string rpc_url{"https://bsc-dataseed.binance.org"};
        std::string api_host;
        std::string websocket_url{"wss://ws.opinion.trade"};
        Contracts contracts;

        // Public data host used by the docs and the TypeScript SDK.
        static Environment bnb_mainnet();
        // Alternate host. Paths are still under `/openapi`.
        static Environment bnb_mainnet_proxy();

        void validate() const;

        // Logical paths are the OpenAPI paths (`/market`, `/token/orderbook`).
        std::string url_for(std::string_view logical_path) const;
        std::string websocket_url_with_key(std::string_view api_key) const;
    };

    inline constexpr int k_chain_id_bnb_mainnet = 56;
    inline constexpr int k_public_rate_limit_per_second = 5;
    inline constexpr int k_authenticated_rate_limit_per_second = 15;
    inline constexpr const char *k_api_key_domain_name = "Opinion OpenAPI";
    inline constexpr const char *k_api_key_domain_version = "1";
    inline constexpr const char *k_order_domain_name = "OPINION CTF Exchange";
    inline constexpr const char *k_order_domain_version = "1";
} // namespace opinion
