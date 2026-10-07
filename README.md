<div align="center">

# opinion-cpp-client

**A lightweight C++20 client for Opinion.trade.**<br>
REST and WebSocket access to the CLOB: markets, order books, EIP-712 signing, and user streams.

[![build](https://github.com/SebastianBoehler/opinion-cpp-client/actions/workflows/build.yml/badge.svg)](https://github.com/SebastianBoehler/opinion-cpp-client/actions/workflows/build.yml)
[![license](https://img.shields.io/github/license/SebastianBoehler/opinion-cpp-client)](LICENSE)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C?logo=cplusplus&logoColor=white)](https://en.cppreference.com/w/cpp/20)
[![CMake](https://img.shields.io/badge/CMake-3.22%2B-064F8C?logo=cmake&logoColor=white)](https://cmake.org)
[![platforms](https://img.shields.io/badge/platforms-Linux%20%7C%20macOS-lightgrey)](#requirements)
[![stars](https://img.shields.io/github/stars/SebastianBoehler/opinion-cpp-client?style=flat&logo=github)](https://github.com/SebastianBoehler/opinion-cpp-client/stargazers)

[Quick start](#quick-start) ·
[Features](#features) ·
[Installation](#installation) ·
[Examples](#examples) ·
[Docs](#documentation)

</div>

---

C++20 trading client for [Opinion.trade](https://opinion.trade) prediction markets. `ClobClient` calls the [OpenAPI](https://docs.opinion.trade/developer-guide/opinion-open-api/overview) CLOB over REST. `WebSocketClient` streams the [market and user channels](https://docs.opinion.trade/developer-guide/opinion-websocket/overview). Public market data does not need an API key. Trading calls need an `apikey` header plus an EIP-712 signed order.

Headers live in `include/opinion/` under namespace `opinion`. This repository does not add endpoints that are absent from the Opinion docs and the official generated client.

## Quick start

```cpp
#include "opinion/opinion.hpp"

#include <iostream>

int main()
{
    opinion::ClobClient client(opinion::Environment::bnb_mainnet());

    opinion::MarketListQuery query;
    query.limit = 5;
    query.status = "activated";

    const auto markets = client.list_markets(query);
    if (!markets)
    {
        std::cerr << "GET /market failed: " << markets.error().message << "\n";
        return 1;
    }
    std::cout << "markets total=" << markets.value().total << "\n";
}
```

The same client reads the order book with `get_orderbook` and quote tokens with `list_quote_tokens`. See [Examples](#examples) for signing and the WebSocket.

## Features

| Area | What you get |
| --- | --- |
| **Market data** | REST markets, books, latest prices, price history, labels, and quote tokens |
| **Trading** | EIP-712 CTF order signing, `prepare_order`, `POST /order`, and `POST /order/cancel` |
| **Account** | API-key create, get, and delete; positions, trades, fees, and balances |
| **WebSocket** | `wss://ws.opinion.trade` with heartbeat, reconnect, and subscription replay |
| **User stream** | Typed `trade.order.update` and `trade.record.new` callbacks |
| **Local book** | REST snapshot plus `market.depth.diff` updates; a zero size removes the level |
| **Errors** | `Result<T>` with typed `SdkError` (transport, HTTP, auth, rate limit, parse) |

## Requirements

- CMake 3.22+ and a C++20 compiler
- libcurl and OpenSSL
- Linux or macOS

[nlohmann/json](https://github.com/nlohmann/json), [IXWebSocket](https://github.com/machinezone/IXWebSocket), [secp256k1](https://github.com/bitcoin-core/secp256k1), and [ethash](https://github.com/chfast/ethash) (Keccak) are fetched and pinned by hash at configure time.

## Installation

### CMake FetchContent

```cmake
include(FetchContent)
FetchContent_Declare(
    opinion_client
    GIT_REPOSITORY https://github.com/SebastianBoehler/opinion-cpp-client.git
    GIT_TAG main # or a release tag
)
FetchContent_MakeAvailable(opinion_client)

target_link_libraries(your_target PRIVATE opinion::client)
```

Check the version at runtime with `opinion::version_string` from `<opinion/version.hpp>`.

### From source

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DOPINION_CLIENT_BUILD_EXAMPLES=ON \
  -DOPINION_CLIENT_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
cmake --install build --prefix <install_prefix>
```

`opinion_tests` is offline. `opinion_markets` calls the public API.

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

Public limit is 5 requests/second per IP. Authenticated limit is 15 requests/second per API key. See [Rate limiting](https://docs.opinion.trade/developer-guide/opinion-open-api/rate-limiting). HTTP 429 is returned as a retryable error when `Retry-After` is present. An invalid `apikey` is HTTP 401 with no anonymous fallback, so public reads do not send the header.

## API key auth

`POST`, `GET`, and `DELETE /auth/api-key` use EIP-712, not the API key. Details: [Authentication](https://docs.opinion.trade/developer-guide/opinion-open-api/authentication).

- Domain: `Opinion OpenAPI`, version `1`, chain id `56`, no `verifyingContract`
- Type `OpinionApiKeyAuth`: `walletAddress` (address), `action` (string), `timestamp` (string seconds)
- Headers: `OPINION_ADDRESS`, `OPINION_SIGNATURE`, `OPINION_TIMESTAMP`
- `action` is `create`, `get`, or `delete` and must match the method
- Signatures expire after 5 minutes. `create` and `delete` are single-use

A new key can take about 15 seconds to become active. A deleted key can take about 10 seconds to stop working. The wallet must already be a registered Opinion account.

Later REST calls send `apikey: <key>`. The same key is the WebSocket `apikey` query parameter.

`opinion_sign` prints an API-key signature when `OPINION_PRIVATE_KEY` is set. It does not send the request.

## Orders

`OrderSigner::sign_order` hashes the CTF `Order` struct over domain `OPINION CTF Exchange` / `1` / chain id 56 / `verifyingContract` = the quote token's exchange:

`salt`, `maker`, `signer`, `taker`, `tokenId`, `makerAmount`, `takerAmount`, `expiration`, `nonce`, `feeRateBps`, `side` (uint8), `signatureType` (uint8).

On-chain side is `0` buy and `1` sell. REST order records use `1` buy and `2` sell. Order type on the wire is `tradingMethod`: `1` market, `2` limit.

The published SDK signs the EIP-712 digest (`keccak256(0x1901 ‖ domainSeparator ‖ hashStruct)`) with raw secp256k1. Builder docs also mention `personal_sign` of a struct hash; `OrderBuilder` in `@opinion-labs/opinion-clob-sdk` does not add the personal-sign prefix. This client follows the SDK.

When `maker` and `signer` differ, `signatureType` defaults to `2` (`POLY_GNOSIS_SAFE`). The official trading client uses the Safe as maker and the EOA as signer. `prepare_order` builds the JSON body and does not post it. `place_order` posts `POST /order` and, if you omit the exchange, loads it from `GET /quoteToken` for the market's quote token.

Limit prices must be in `(0, 1)` with at most 6 decimal places. Amounts are scaled with the quote token's decimals (18 for BSC USDT). Set exactly one of `maker_amount_in_quote_token` or `maker_amount_in_base_token`. Market buys take quote amount only. Market sells take base amount only. `post_only` is limit-only.

## WebSocket

`WebSocketClient` is a native socket (IXWebSocket) for `wss://ws.opinion.trade`. It sends `{"action":"HEARTBEAT"}` about every 30 seconds and can replay subscriptions after reconnect. Channel reference: [market channels](https://docs.opinion.trade/developer-guide/opinion-websocket/market-channels) and [user channels](https://docs.opinion.trade/developer-guide/opinion-websocket/user-channels).

| Channel | Subscribe with |
| --- | --- |
| `market.depth.diff` | `marketId` (one binary market) |
| `market.last.price` | `marketId` or `rootMarketId` |
| `market.last.trade` | `marketId` or `rootMarketId` |
| `trade.order.update` | `marketId` or `rootMarketId` |
| `trade.record.new` | `marketId` or `rootMarketId` |

A matched trade is not an on-chain fill. `trade.record.new` is the confirmed fill channel. `LocalOrderbook` replaces the book from a REST snapshot and applies `market.depth.diff` one level at a time. A size that parses as zero removes that level; the channel docs list the fields and do not state the zero-size rule.

`opinion_ws` prints the subscribe payload. With `OPINION_API_KEY` set, it connects, subscribes, sends one heartbeat, and disconnects.

```cpp
opinion::WebSocketClient socket;
socket.set_url(environment.websocket_url_with_key(api_key));
socket.subscribe(opinion::WebSocketClient::subscribe_message(
    opinion::k_channel_depth_diff, market_id, false));
```

On-chain split, merge, redeem, and `enableTrading` are not wrapped. Those need a BSC transaction and the conditional-token ABI.

## Examples

Build with `-DOPINION_CLIENT_BUILD_EXAMPLES=ON`. Binaries land in `build/`.

| Example | What it does |
| --- | --- |
| `opinion_markets` | Lists activated markets, one order book, and quote tokens (public REST) |
| `opinion_sign` | Prints an `OpinionApiKeyAuth` signature (`OPINION_PRIVATE_KEY`); local only |
| `opinion_ws` | Prints the depth subscribe payload, or connects when `OPINION_API_KEY` is set |

## Documentation

| Guide | Topic |
| --- | --- |
| [Opinion docs](https://docs.opinion.trade/) | Product and developer documentation |
| [OpenAPI overview](https://docs.opinion.trade/developer-guide/opinion-open-api/overview) | REST host, envelope, and routes |
| [Authentication](https://docs.opinion.trade/developer-guide/opinion-open-api/authentication) | EIP-712 API-key headers |
| [Market](https://docs.opinion.trade/developer-guide/opinion-open-api/market) | Market list and detail |
| [Order](https://docs.opinion.trade/developer-guide/opinion-open-api/order) | Order queries |
| [WebSocket overview](https://docs.opinion.trade/developer-guide/opinion-websocket/overview) | Socket URL and heartbeat |
| [Market channels](https://docs.opinion.trade/developer-guide/opinion-websocket/market-channels) | Depth, price, and trade channels |
| [User channels](https://docs.opinion.trade/developer-guide/opinion-websocket/user-channels) | Order updates and confirmed fills |

## Contributing

Contributions are welcome. Build and test with the commands in [From source](#from-source). Pull request titles use `type: description` or `type(scope): description` (`feat`, `fix`, `docs`, `style`, `refactor`, `perf`, `test`, `build`, `ci`, `chore`, `revert`). CI configures CMake and builds the library, examples, and tests on Linux and macOS.

Release tags are `vX.Y.Z` and must match `project(... VERSION X.Y.Z)` in `CMakeLists.txt` and `opinion::version_string` in `include/opinion/version.hpp`. Pushing that tag runs the Release workflow: it checks the version, builds, tests, and publishes the GitHub release.

Bugs go to [Issues](https://github.com/SebastianBoehler/opinion-cpp-client/issues). Security reports follow [SECURITY.md](SECURITY.md).

## Disclaimer

This is an independent open-source project, not affiliated with or endorsed by Opinion Labs. Trading involves risk of loss. You are responsible for complying with Opinion.trade's terms and the laws of your jurisdiction.

## License

[MIT](LICENSE)
