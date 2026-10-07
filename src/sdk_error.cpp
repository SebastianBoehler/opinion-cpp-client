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

    SdkError make_invalid_argument(const std::string &message, const std::string &endpoint)
    {
        SdkError error;
        error.code = SdkErrorCode::InvalidArgument;
        error.message = message;
        error.endpoint = endpoint;
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

    SdkError make_signing_error(const std::string &message)
    {
        SdkError error;
        error.code = SdkErrorCode::Signing;
        error.message = message;
        return error;
    }

    SdkError make_auth_error(const std::string &message, const std::string &endpoint)
    {
        SdkError error;
        error.code = SdkErrorCode::Auth;
        error.message = message;
        error.endpoint = endpoint;
        return error;
    }
} // namespace opinion
