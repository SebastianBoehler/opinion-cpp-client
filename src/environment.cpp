#include "opinion/environment.hpp"

#include <cctype>
#include <stdexcept>

namespace opinion
{
    namespace
    {
        bool ends_with(std::string_view value, std::string_view suffix)
        {
            return value.size() >= suffix.size() &&
                   value.substr(value.size() - suffix.size()) == suffix;
        }

        std::string trim_slashes_right(std::string value)
        {
            while (!value.empty() && value.back() == '/')
            {
                value.pop_back();
            }
            return value;
        }

        std::string normalize_logical_path(std::string_view logical_path)
        {
            std::string path(logical_path);
            if (path.empty())
            {
                throw std::invalid_argument("API path is empty");
            }
            if (path.front() != '/')
            {
                path.insert(path.begin(), '/');
            }
            if (path.rfind("/openapi/", 0) == 0)
            {
                path = path.substr(std::string("/openapi").size());
            }
            else if (path == "/openapi")
            {
                path = "/";
            }
            return path;
        }
    } // namespace

    Environment Environment::bnb_mainnet()
    {
        Environment environment;
        environment.name = "bnb-mainnet";
        environment.chain_id = k_chain_id_bnb_mainnet;
        environment.api_host = "https://openapi.opinion.trade/openapi";
        environment.websocket_url = "wss://ws.opinion.trade";
        environment.rpc_url = "https://bsc-dataseed.binance.org";
        return environment;
    }

    Environment Environment::bnb_mainnet_proxy()
    {
        Environment environment = bnb_mainnet();
        environment.name = "bnb-mainnet-proxy";
        environment.api_host = "https://proxy.opinion.trade:8443";
        return environment;
    }

    void Environment::validate() const
    {
        if (chain_id != k_chain_id_bnb_mainnet)
        {
            throw std::invalid_argument("Opinion OpenAPI documents BNB Chain mainnet (chain id 56) only");
        }
        if (api_host.rfind("https://", 0) != 0)
        {
            throw std::invalid_argument("api_host must be an https URL");
        }
        if (websocket_url.rfind("wss://", 0) != 0)
        {
            throw std::invalid_argument("websocket_url must be a wss URL");
        }
    }

    std::string Environment::url_for(std::string_view logical_path) const
    {
        const std::string path = normalize_logical_path(logical_path);
        std::string host = trim_slashes_right(api_host);
        if (!ends_with(host, "/openapi"))
        {
            host += "/openapi";
        }
        if (path == "/")
        {
            return host;
        }
        return host + path;
    }

    std::string Environment::websocket_url_with_key(std::string_view api_key) const
    {
        std::string url = trim_slashes_right(websocket_url);
        url += url.find('?') == std::string::npos ? "?apikey=" : "&apikey=";
        static constexpr char hex[] = "0123456789ABCDEF";
        for (unsigned char byte : api_key)
        {
            if (std::isalnum(byte) || byte == '-' || byte == '_' || byte == '.' || byte == '~')
            {
                url.push_back(static_cast<char>(byte));
            }
            else
            {
                url.push_back('%');
                url.push_back(hex[byte >> 4]);
                url.push_back(hex[byte & 0x0f]);
            }
        }
        return url;
    }
} // namespace opinion
