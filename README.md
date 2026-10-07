# opinion-cpp-client

C++20 client for [Opinion.trade](https://opinion.trade) prediction markets. The layout follows [polymarket-cpp-client](https://github.com/SebastianBoehler/polymarket-cpp-client): public headers live in `include/opinion/` under namespace `opinion`, with the library, examples, and tests beside them.

Public market data does not need an API key. Trading calls need an `apikey` header plus an EIP-712 signed order. This repository does not add endpoints that are absent from the Opinion docs and the official generated client.

## Build

Dependencies fetched by CMake: [nlohmann/json](https://github.com/nlohmann/json), [IXWebSocket](https://github.com/machinezone/IXWebSocket), [secp256k1](https://github.com/bitcoin-core/secp256k1), and [ethash](https://github.com/chfast/ethash) (Keccak). The system also needs libcurl and OpenSSL.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/opinion_tests
./build/opinion_markets
```

`opinion_markets` calls the public API:

- `GET https://openapi.opinion.trade/openapi/market`
- `GET https://openapi.opinion.trade/openapi/token/orderbook`
- `GET https://openapi.opinion.trade/openapi/quoteToken`

Consume the library from another CMake project with `FetchContent` and `opinion::client`.

```cmake
include(FetchContent)
FetchContent_Declare(
    opinion_client
    GIT_REPOSITORY https://github.com/SebastianBoehler/opinion-cpp-client.git
    GIT_TAG main
)
FetchContent_MakeAvailable(opinion_client)
target_link_libraries(your_target PRIVATE opinion::client)
```

## Hosts

Two bases show up in the wild, and they do not take the same path prefix.

| Preset | Base | Logical path `/market` becomes |
| --- | --- | --- |
| `Environment::bnb_mainnet()` | `https://openapi.opinion.trade/openapi` | `https://openapi.opinion.trade/openapi/market` |
| `Environment::bnb_mainnet_proxy()` | `https://proxy.opinion.trade:8443` | `https://proxy.opinion.trade:8443/openapi/market` |

The TypeScript SDK default (`DEFAULT_API_HOST`) already ends in `/openapi`. The community Go client defaults to `https://proxy.opinion.trade:8443` and then prefixes every path with `/openapi`. `Environment::url_for()` accepts the logical OpenAPI path (`/market`, `/token/orderbook`, `/order`) and adds `/openapi` only when the host does not already end with it. Sending `/openapi/openapi/market` is a 404.

```cpp
opinion::ClobClient client(opinion::Environment::bnb_mainnet());
// Same calls work against the proxy host:
// opinion::ClobClient client(opinion::Environment::bnb_mainnet_proxy());
```

WebSocket is separate from either REST host: `wss://ws.opinion.trade?apikey=...`.

Chain id is BNB Chain mainnet **56**. The documented HTTP RPC example is `https://bsc-dataseed.binance.org`. Contract addresses published with the TypeScript SDK:

| Contract | Address |
| --- | --- |
| ConditionalTokens | `0xAD1a38cEc043e70E83a3eC30443dB285ED10D774` |
| MultiSend | `0x38869bf66a61cF6bDB996A6aE40D5853Fd43B526` |
| FeeManager | `0xC9063Dc52dEEfb518E5b6634A6b8D624bc5d7c36` |

The CTF exchange is **not** in that table. Read `ctfExchangeAddress` from `GET /quoteToken`. On BSC the USDT quote (`0x55d398326f99059fF775485246999027B3197955`) is 18 decimals, not 6.

## REST

`ClobClient` speaks the logical paths below. Responses are accepted whether the envelope uses `errno` / `errmsg` (what the live API and the auth pages return) or `code` / `msg` (what some data pages still show). Success is `0`.

| Method | Path | Auth |
| --- | --- | --- |
| GET | `/market` | public |
| GET | `/market/{marketId}` | public |
| GET | `/market/categorical/{marketId}` | public |
| GET | `/market/slug/{slug}` | public |
| GET | `/label` | public |
| GET | `/token/latest-price` | public |
| GET | `/token/orderbook` | public |
| GET | `/token/price-history` | public |
| GET | `/quoteToken` | public |
| POST, GET, DELETE | `/auth/api-key` | EIP-712 wallet headers |
| GET | `/order` | `apikey` |
| GET | `/order/{orderId}` | `apikey` |
| POST | `/order` | `apikey` |
| POST | `/order/cancel` | `apikey` |
| GET | `/positions` | `apikey` |
| GET | `/positions/user/{walletAddress}` | `apikey` |
| GET | `/trade` | `apikey` |
| GET | `/trade/user/{walletAddress}` | `apikey` |
| GET | `/token/fee-rates` | `apikey` |
| GET | `/user/auth` | `apikey` |
| GET | `/user/balance` | `apikey` |

`POST /order` and `POST /order/cancel` are in the official `@opinion-labs/opinion-api` client and the CLOB SDK. The GitBook order page only documents the GET routes. Place and cancel here follow that generated client: `V2AddOrderReq` (`topicId`, `tradingMethod`, `currencyAddress`, signed CTF fields, optional `postOnly`) and `{"orderId": "..."}`.

`/topic`, `/topic/{topicId}`, and `/topic/multi/{topicId}` exist on the generated client and are not wrapped. The GitBook market pages are the `/market` routes above.

Public limit is 5 requests/second per IP. Authenticated limit is 15 requests/second per API key. HTTP 429 is returned as a retryable error when `Retry-After` is present. An invalid `apikey` is HTTP 401 with no anonymous fallback, so public reads do not send the header.

## API key auth

`POST`, `GET`, and `DELETE /auth/api-key` use EIP-712, not the API key.

- Domain: `Opinion OpenAPI`, version `1`, chain id `56`, no `verifyingContract`
- Type `OpinionApiKeyAuth`: `walletAddress` (address), `action` (string), `timestamp` (string seconds)
- Headers: `OPINION_ADDRESS`, `OPINION_SIGNATURE`, `OPINION_TIMESTAMP`
- `action` is `create`, `get`, or `delete` and must match the method
- Signatures expire after 5 minutes. `create` and `delete` are single-use

A new key can take about 15 seconds to become active. A deleted key can take about 10 seconds to stop working. The wallet must already be a registered Opinion account.

Later REST calls send `apikey: <key>`. The same key is the WebSocket `apikey` query parameter.

## Orders

`OrderSigner::sign_order` hashes the CTF `Order` struct over domain `OPINION CTF Exchange` / `1` / chain id 56 / `verifyingContract` = the quote token's exchange:

`salt`, `maker`, `signer`, `taker`, `tokenId`, `makerAmount`, `takerAmount`, `expiration`, `nonce`, `feeRateBps`, `side` (uint8), `signatureType` (uint8).

On-chain side is `0` buy and `1` sell. REST order records use `1` buy and `2` sell. Order type on the wire is `tradingMethod`: `1` market, `2` limit.

The published SDK signs the EIP-712 digest (`keccak256(0x1901 ‖ domainSeparator ‖ hashStruct)`) with raw secp256k1. Builder docs also mention `personal_sign` of a struct hash; `OrderBuilder` in `@opinion-labs/opinion-clob-sdk` does not add the personal-sign prefix. This client follows the SDK.

When `maker` and `signer` differ, `signatureType` defaults to `2` (`POLY_GNOSIS_SAFE`). The official trading client uses the Safe as maker and the EOA as signer. `prepare_order` builds the JSON body and does not post it. `place_order` posts `POST /order` and, if you omit the exchange, loads it from `GET /quoteToken` for the market's quote token.

Limit prices must be in `(0, 1)` with at most 6 decimal places. Amounts are scaled with the quote token's decimals (18 for BSC USDT). Set exactly one of `maker_amount_in_quote_token` or `maker_amount_in_base_token`. Market buys take quote amount only. Market sells take base amount only. `post_only` is limit-only.

`opinion_sign` prints an API-key signature when `OPINION_PRIVATE_KEY` is set. It does not send the request.

## WebSocket

`WebSocketClient` is a native socket (IXWebSocket) for `wss://ws.opinion.trade`. It sends `{"action":"HEARTBEAT"}` about every 30 seconds and can replay subscriptions after reconnect.

| Channel | Subscribe with |
| --- | --- |
| `market.depth.diff` | `marketId` (one binary market) |
| `market.last.price` | `marketId` or `rootMarketId` |
| `market.last.trade` | `marketId` or `rootMarketId` |
| `trade.order.update` | `marketId` or `rootMarketId` |
| `trade.record.new` | `marketId` or `rootMarketId` |

A matched trade is not an on-chain fill. `trade.record.new` is the confirmed fill channel. `LocalOrderbook` replaces the book from a REST snapshot and applies `market.depth.diff` one level at a time. A size that parses as zero removes that level; the channel docs list the fields and do not state the zero-size rule.

`opinion_ws` prints the subscribe payload. With `OPINION_API_KEY` set, it connects, subscribes, sends one heartbeat, and disconnects.

On-chain split, merge, redeem, and `enableTrading` are not wrapped. Those need a BSC transaction and the conditional-token ABI.

## License

MIT. See [LICENSE](LICENSE). This project is not affiliated with Opinion Labs.
