#include "opinion/sdk_error.hpp"

#include <algorithm>

namespace opinion
{
    std::string sdk_error_code_to_string(SdkErrorCode code)
    {
        switch (code)
        {
        case SdkErrorCode::HttpTransport:
            return "http_transport";
        case SdkErrorCode::ApiResponse:
            return "api_response";
        case SdkErrorCode::Auth:
            return "auth";
        case SdkErrorCode::RateLimit:
            return "rate_limit";
        case SdkErrorCode::Parse:
            return "parse";
        case SdkErrorCode::Signing:
            return "signing";
        case SdkErrorCode::InvalidArgument:
            return "invalid_argument";
        }
        return "unknown";
    }

    SdkError make_transport_error(const std::string &message, const std::string &endpoint)
    {
        SdkError error;
        error.code = SdkErrorCode::HttpTransport;
        error.message = message;
        error.endpoint = endpoint;
        error.retryable = true;
        return error;
    }

    SdkError make_api_error(int api_code,
                            const std::string &message,
                            const std::string &endpoint,
                            long http_status,
                            const std::string &body)
    {
        SdkError error;
        error.code = SdkErrorCode::ApiResponse;
        error.api_code = api_code;
        error.message = message.empty() ? "API error " + std::to_string(api_code) : message;
        error.endpoint = endpoint;
        error.http_status = http_status;
        error.response_body_excerpt = body.substr(0, std::min<std::size_t>(body.size(), 512));
        return error;
    }

    SdkError make_http_status_error(long http_status,
                                    const std::string &endpoint,
                                    const std::string &body,
                                    const std::string &retry_after)
    {
        SdkError error;
        error.http_status = http_status;
        error.endpoint = endpoint;
        error.response_body_excerpt = body.substr(0, std::min<std::size_t>(body.size(), 512));
        if (http_status == 401)
        {
            error.code = SdkErrorCode::Auth;
            error.message = "unauthorized (invalid or missing apikey)";
        }
        else if (http_status == 429)
        {
            error.code = SdkErrorCode::RateLimit;
            error.retryable = true;
            error.message = retry_after.empty() ? "rate limit exceeded" : "rate limit exceeded; retry after " + retry_after;
        }
        else
        {
            error.code = SdkErrorCode::HttpTransport;
            error.message = "HTTP " + std::to_string(http_status);
            error.retryable = http_status >= 500;
        }
        return error;
    }

    SdkError make_parse_error(const std::string &message, const std::string &endpoint, const std::string &body)
    {
        SdkError error;
        error.code = SdkErrorCode::Parse;
        error.message = message;
        error.endpoint = endpoint;
        error.response_body_excerpt = body.substr(0, std::min<std::size_t>(body.size(), 512));
        return error;
    }

    SdkError make_invalid_argument(const std::string &message)
    {
        SdkError error;
        error.code = SdkErrorCode::InvalidArgument;
        error.message = message;
        return error;
    }

    SdkError make_signing_error(const std::string &message)
    {
        SdkError error;
        error.code = SdkErrorCode::Signing;
        error.message = message;
        return error;
    }
} // namespace opinion
