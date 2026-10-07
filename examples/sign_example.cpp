#include "opinion/opinion.hpp"

#include <iostream>
#include <cstdlib>

int main()
{
    const char *key = std::getenv("OPINION_PRIVATE_KEY");
    if (!key || key[0] == '\0')
    {
        std::cout << "Set OPINION_PRIVATE_KEY to print an OpinionApiKeyAuth signature.\n"
                  << "The signature is local. This example does not call the API.\n";
        return 0;
    }
    const opinion::OrderSigner signer(key);
    const auto headers = signer.sign_api_key_auth(opinion::ApiKeyAction::Create);
    std::cout << "address " << signer.address() << "\n"
              << "OPINION_TIMESTAMP " << headers.opinion_timestamp << "\n"
              << "OPINION_SIGNATURE " << headers.opinion_signature << "\n"
              << "digest " << headers.digest << "\n";
    return 0;
}
