#include "opinion/http_client.hpp"

#include "opinion/version.hpp"

#include <curl/curl.h>

#include <atomic>
#include <chrono>
#include <stdexcept>
#include <utility>

namespace opinion
{
    namespace
    {
        std::atomic<int> g_curl_users{0};

        struct HeaderCapture
        {
            std::map<std::string, std::string> *headers{nullptr};
        };

        std::size_t write_body(char *ptr, std::size_t size, std::size_t nmemb, void *userdata)
        {
            const std::size_t bytes = size * nmemb;
            auto *body = static_cast<std::string *>(userdata);
            body->append(ptr, bytes);
            return bytes;
        }

        std::size_t write_header(char *ptr, std::size_t size, std::size_t nmemb, void *userdata)
        {
            const std::size_t bytes = size * nmemb;
            auto *capture = static_cast<HeaderCapture *>(userdata);
            std::string line(ptr, bytes);
            while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
                line.pop_back();
            const auto colon = line.find(':');
            if (colon == std::string::npos || !capture->headers)
                return bytes;
            std::string name = line.substr(0, colon);
            std::string value = line.substr(colon + 1);
            while (!value.empty() && value.front() == ' ')
                value.erase(value.begin());
            for (char &c : name)
            {
                if (c >= 'A' && c <= 'Z')
                    c = static_cast<char>(c - 'A' + 'a');
            }
            (*capture->headers)[name] = value;
            return bytes;
        }
    } // namespace

    struct HttpClient::State
    {
        CURL *curl{nullptr};
    };

    void http_global_init()
    {
        if (g_curl_users.fetch_add(1) == 0)
            curl_global_init(CURL_GLOBAL_DEFAULT);
    }

    void http_global_cleanup()
    {
        if (g_curl_users.fetch_sub(1) == 1)
            curl_global_cleanup();
    }

    std::string HttpResponse::header(const std::string &name) const
    {
        std::string key = name;
        for (char &c : key)
        {
            if (c >= 'A' && c <= 'Z')
                c = static_cast<char>(c - 'A' + 'a');
        }
        const auto it = headers.find(key);
        return it == headers.end() ? std::string{} : it->second;
    }

    HttpClient::HttpClient() : HttpClient(HttpClientOptions{}) {}

    HttpClient::HttpClient(HttpClientOptions options) : options_(std::move(options))
    {
        http_global_init();
        owns_global_ = true;
        state_ = new State();
        state_->curl = curl_easy_init();
        if (!state_->curl)
            throw std::runtime_error("curl_easy_init failed");
        if (options_.user_agent.empty())
            options_.user_agent = kUserAgent;
        apply_options();
    }

    HttpClient::~HttpClient()
    {
        if (state_)
        {
            if (state_->curl)
                curl_easy_cleanup(state_->curl);
            delete state_;
            state_ = nullptr;
        }
        if (owns_global_)
            http_global_cleanup();
    }

    HttpClient::HttpClient(HttpClient &&other) noexcept
        : state_(other.state_),
          base_url_(std::move(other.base_url_)),
          options_(std::move(other.options_)),
          headers_(std::move(other.headers_)),
          last_(other.last_),
          owns_global_(other.owns_global_)
    {
        other.state_ = nullptr;
        other.owns_global_ = false;
    }

    HttpClient &HttpClient::operator=(HttpClient &&other) noexcept
    {
        if (this == &other)
            return *this;
        this->~HttpClient();
        new (this) HttpClient(std::move(other));
        return *this;
    }

    void HttpClient::set_base_url(const std::string &base_url)
    {
        std::lock_guard lock(mutex_);
        base_url_ = base_url;
        while (!base_url_.empty() && base_url_.back() == '/')
            base_url_.pop_back();
    }

    void HttpClient::set_proxy(const std::string &proxy_url)
    {
        std::lock_guard lock(mutex_);
        options_.proxy_url = proxy_url;
        apply_options();
    }

    void HttpClient::set_header(const std::string &name, const std::string &value)
    {
        std::lock_guard lock(mutex_);
        headers_[name] = value;
    }

    void HttpClient::clear_header(const std::string &name)
    {
        std::lock_guard lock(mutex_);
        headers_.erase(name);
    }

    HttpClientOptions HttpClient::options() const
    {
        std::lock_guard lock(mutex_);
        return options_;
    }

    RequestMetrics HttpClient::last_metrics() const
    {
        std::lock_guard lock(mutex_);
        return last_;
    }

    void HttpClient::apply_options()
    {
        CURL *curl = state_->curl;
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
        curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, options_.tcp_keepalive ? 1L : 0L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, options_.timeout_ms);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, options_.connect_timeout_ms);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, options_.user_agent.c_str());
        curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
        if (!options_.proxy_url.empty())
            curl_easy_setopt(curl, CURLOPT_PROXY, options_.proxy_url.c_str());
        else
            curl_easy_setopt(curl, CURLOPT_PROXY, nullptr);
    }

    HttpResponse HttpClient::get(const std::string &path) { return perform("GET", path, {}); }

    HttpResponse HttpClient::post(const std::string &path, const std::string &body)
    {
        return perform("POST", path, body);
    }

    HttpResponse HttpClient::del(const std::string &path, const std::string &body)
    {
        return perform("DELETE", path, body);
    }

    HttpResponse HttpClient::perform(const std::string &method, const std::string &path, const std::string &body)
    {
        std::lock_guard lock(mutex_);
        HttpResponse response;
        std::string url = path;
        if (path.rfind("http://", 0) != 0 && path.rfind("https://", 0) != 0)
        {
            if (base_url_.empty())
            {
                response.error = "HTTP base URL is empty";
                return response;
            }
            url = base_url_;
            if (path.empty() || path.front() != '/')
                url.push_back('/');
            url += path;
        }

        CURL *curl = state_->curl;
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPGET, method == "GET" ? 1L : 0L);
        if (method == "POST" || method == "DELETE")
        {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
        }
        else
        {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, nullptr);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, 0L);
        }
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_body);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
        HeaderCapture capture{&response.headers};
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, write_header);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &capture);

        struct curl_slist *list = nullptr;
        bool has_content_type = false;
        for (const auto &[name, value] : headers_)
        {
            if (name == "Content-Type" || name == "content-type")
                has_content_type = true;
            const std::string line = name + ": " + value;
            list = curl_slist_append(list, line.c_str());
        }
        if ((method == "POST" || (method == "DELETE" && !body.empty())) && !has_content_type)
            list = curl_slist_append(list, "Content-Type: application/json");
        list = curl_slist_append(list, "Accept: application/json");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);

        const auto started = std::chrono::steady_clock::now();
        const CURLcode rc = curl_easy_perform(curl);
        const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started);
        curl_slist_free_all(list);

        response.elapsed_ms = elapsed.count();
        if (rc != CURLE_OK)
            response.error = curl_easy_strerror(rc);
        else
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status_code);

        long reused = 0;
        curl_easy_getinfo(curl, CURLINFO_NUM_CONNECTS, &reused);
        last_.method = method;
        last_.url = url;
        last_.status_code = response.status_code;
        last_.elapsed_ms = response.elapsed_ms;
        last_.reused_connection = response.ok() && reused == 0;
        return response;
    }
} // namespace opinion
