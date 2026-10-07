#pragma once

#include "opinion/clob_client.hpp"

namespace opinion
{
    // Read-only portfolio helper. On-chain split, merge, and redeem are not
    // wrapped here: those calls need a BSC RPC and the conditional-token ABI,
    // which this client does not embed.
    class PositionClient
    {
    public:
        explicit PositionClient(ClobClient &client) : client_(&client) {}

        Result<PositionList> list_mine(const PageQuery &query = {}) const
        {
            return client_->list_positions(query);
        }

        Result<PositionList> list_for(const std::string &wallet_address, const PageQuery &query = {}) const
        {
            return client_->list_positions(wallet_address, query);
        }

    private:
        ClobClient *client_;
    };
} // namespace opinion
