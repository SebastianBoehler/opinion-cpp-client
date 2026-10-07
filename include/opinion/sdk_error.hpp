#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace opinion
{
    enum class SdkErrorCode
    {
        HttpTransport,
        ApiResponse,
        Auth,
        RateLimit,
        Parse,
        Signing,
        InvalidArgument
    };

    struct SdkError
    {
        SdkErrorCode code{SdkErrorCode::ApiResponse};
        std::string message;
        std::string endpoint;
        long http_status{0};
        int business_code{0};
        std::string response_body_excerpt;
        std::string request_id;
        bool retryable{false};
    };

    template <typename T>
    class Result
    {
    public:
        static Result<T> success(T value)
        {
            Result<T> result;
            result.value_ = std::move(value);
            return result;
        }

        static Result<T> failure(SdkError error)
        {
            Result<T> result;
            result.error_ = std::move(error);
            return result;
        }

        bool ok() const { return value_.has_value(); }
        explicit operator bool() const { return ok(); }

        const T &value() const
        {
            if (!value_)
                throw std::logic_error("Result does not contain a value");
            return *value_;
        }

        T &value()
        {
            if (!value_)
                throw std::logic_error("Result does not contain a value");
            return *value_;
        }

        const SdkError &error() const
        {
            if (!error_)
                throw std::logic_error("Result does not contain an error");
            return *error_;
        }

    private:
        std::optional<T> value_;
        std::optional<SdkError> error_;
    };

    std::string sdk_error_code_to_string(SdkErrorCode code);

    SdkError make_invalid_argument(const std::string &message, const std::string &endpoint = {});
    SdkError make_parse_error(const std::string &message,
                              const std::string &endpoint,
                              const std::string &body = {});
    SdkError make_signing_error(const std::string &message);
    SdkError make_auth_error(const std::string &message, const std::string &endpoint = {});
} // namespace opinion
