#pragma once
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <utility>
namespace opinion::detail
{
    using json = nlohmann::json;
    struct Envelope
    {
        int code{0};
        std::string message;
        json result = json::object();
    };

    inline Envelope parse_envelope(const std::string &body)
    {
        json document = json::parse(body);
        if (!document.is_object())
        {
            throw std::runtime_error("response is not a JSON object");
        }
        Envelope envelope;
        if (document.contains("errno") && !document["errno"].is_null())
        {
            envelope.code = document["errno"].get<int>();
            envelope.message = document.value("errmsg", std::string());
        }
        else if (document.contains("code") && !document["code"].is_null())
        {
            envelope.code = document["code"].get<int>();
            envelope.message = document.value("msg", std::string());
        }
        if (document.contains("result") && !document["result"].is_null())
        {
            envelope.result = std::move(document["result"]);
        }
        return envelope;
    }

} // namespace opinion::detail
