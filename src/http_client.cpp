#include "opinion/http_client.hpp"

#include <curl/curl.h>

#include <cctype>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace opinion
{
    namespace
    {
        void ensure_curl()
        {
            static std::once_flag once;
            std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
        }

        std::string trim_copy(std::string value)
        {
            while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
            {
                value.erase(value.begin());
            }
            while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
            {
                value.pop_back();
            }
            return value;
        }

        std::string lower_copy(std::string value)
        {
            for (char &character : value)
            {
                character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
            }
            return value;
        }

        std::size_t write_callback(char *ptr, std::size_t size, std::size_t count, void *userdata)
        {
            auto *body = static_cast<std::string *>(userdata);
            body->append(ptr, size * count);
            return size * count;
        }

        std::size_t header_callback(char *ptr, std::size_t size, std::size_t count, void *userdata)
        {
            auto *headers = static_cast<std::map<std::string, std::string> *>(userdata);
            std::string line(ptr, size * count);
            const auto colon = line.find(':');
            if (colon != std::string::npos)
            {
                std::string name = lower_copy(trim_copy(line.substr(0, colon)));
                std::string value = trim_copy(line.substr(colon + 1));
                if (!name.empty())
                {
                    (*headers)[name] = std::move(value);
                }
            }
            return size * count;
        }

        std::string effective_proxy(const HttpClientOptions &options)
        {
            if (!options.proxy_url.empty())
            {
                return options.proxy_url;
            }
            return default_network_route().proxy_url;
        }

        std::string effective_interface(const HttpClientOptions &options)
        {
            if (!options.interface_name.empty())
            {
                return options.interface_name;
            }
            return default_network_route().interface_name;
        }
    } // namespace

    struct HttpClient::Impl
    {
        HttpClientOptions options;
        CURL *curl{nullptr};
        std::mutex mutex;

        Impl()
        {
            ensure_curl();
            curl = curl_easy_init();
            if (!curl)
            {
                throw std::runtime_error("curl_easy_init failed");
            }
        }

        ~Impl()
        {
            if (curl)
            {
                curl_easy_cleanup(curl);
            }
        }
    };

    HttpClient::HttpClient() : impl_(std::make_unique<Impl>()) {}

    HttpClient::HttpClient(HttpClientOptions options) : HttpClient()
    {
        configure(std::move(options));
    }

    HttpClient::~HttpClient() = default;
    HttpClient::HttpClient(HttpClient &&) noexcept = default;
    HttpClient &HttpClient::operator=(HttpClient &&) noexcept = default;

    void HttpClient::configure(const HttpClientOptions &options)
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->options = options;
    }

    const HttpClientOptions &HttpClient::options() const
    {
        return impl_->options;
    }

    HttpResponse HttpClient::request(const std::string &method,
                                     const std::string &url,
                                     const std::string &body,
                                     const std::map<std::string, std::string> &headers)
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        HttpResponse response;
        curl_easy_reset(impl_->curl);
        curl_easy_setopt(impl_->curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(impl_->curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(impl_->curl, CURLOPT_TIMEOUT_MS, impl_->options.timeout_ms);
        curl_easy_setopt(impl_->curl, CURLOPT_CONNECTTIMEOUT_MS, impl_->options.connect_timeout_ms);
        curl_easy_setopt(impl_->curl, CURLOPT_TCP_KEEPALIVE, impl_->options.tcp_keepalive ? 1L : 0L);
        curl_easy_setopt(impl_->curl, CURLOPT_USERAGENT, impl_->options.user_agent.c_str());
        curl_easy_setopt(impl_->curl, CURLOPT_FOLLOWLOCATION, 0L);
        curl_easy_setopt(impl_->curl, CURLOPT_ACCEPT_ENCODING, "");
        curl_easy_setopt(impl_->curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(impl_->curl, CURLOPT_WRITEDATA, &response.body);
        curl_easy_setopt(impl_->curl, CURLOPT_HEADERFUNCTION, header_callback);
        curl_easy_setopt(impl_->curl, CURLOPT_HEADERDATA, &response.headers);

        const std::string proxy = effective_proxy(impl_->options);
        if (!proxy.empty())
        {
            curl_easy_setopt(impl_->curl, CURLOPT_PROXY, proxy.c_str());
        }
        const std::string interface_name = effective_interface(impl_->options);
        if (!interface_name.empty())
        {
            curl_easy_setopt(impl_->curl, CURLOPT_INTERFACE, interface_name.c_str());
        }

        if (method == "POST")
        {
            curl_easy_setopt(impl_->curl, CURLOPT_POST, 1L);
            curl_easy_setopt(impl_->curl, CURLOPT_POSTFIELDS, body.data());
            curl_easy_setopt(impl_->curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
        }
        else if (method != "GET")
        {
            curl_easy_setopt(impl_->curl, CURLOPT_CUSTOMREQUEST, method.c_str());
            if (!body.empty())
            {
                curl_easy_setopt(impl_->curl, CURLOPT_POSTFIELDS, body.data());
                curl_easy_setopt(impl_->curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
            }
        }

        curl_slist *header_list = nullptr;
        for (const auto &[name, value] : headers)
        {
            const std::string line = name + ": " + value;
            header_list = curl_slist_append(header_list, line.c_str());
        }
        if (header_list)
        {
            curl_easy_setopt(impl_->curl, CURLOPT_HTTPHEADER, header_list);
        }

        char error_buffer[CURL_ERROR_SIZE] = {};
        curl_easy_setopt(impl_->curl, CURLOPT_ERRORBUFFER, error_buffer);
        const CURLcode code = curl_easy_perform(impl_->curl);
        if (header_list)
        {
            curl_slist_free_all(header_list);
        }
        if (code != CURLE_OK)
        {
            response.error = error_buffer[0] ? error_buffer : curl_easy_strerror(code);
            return response;
        }
        curl_easy_getinfo(impl_->curl, CURLINFO_RESPONSE_CODE, &response.status_code);
        double seconds = 0;
        curl_easy_getinfo(impl_->curl, CURLINFO_TOTAL_TIME, &seconds);
        response.elapsed_ms = seconds * 1000.0;
        return response;
    }
} // namespace opinion
