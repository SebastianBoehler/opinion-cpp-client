# opinion-cpp-client

C++20 SDK for [Opinion.trade](https://opinion.trade) prediction markets: public OpenAPI market data, EIP-712 API-key auth, CLOB order place/cancel, and WebSocket market channels.

Suggested GitHub topics: `opinion-trade` `prediction-markets` `clob` `eip-712` `bnb-chain` `cpp` `websocket` `trading`

Modeled on [polymarket-cpp-client](https://github.com/SebastianBoehler/polymarket-cpp-client) (`include/opinion/`, namespace `opinion::`). This is a v1 scaffold. It speaks the endpoints documented by Opinion and confirmed in the official Python SDK (`opinion_clob_sdk` 0.7.0) and the generated `opinion_api` 0.4.0 client. It does not invent routes.

## Install

System packages: a C++20 compiler, CMake 3.22+, libcurl, and OpenSSL.

```bash
sudo apt-get install build-essential cmake libcurl4-openssl-dev libssl-dev pkg-config
git clone https://github.com/SebastianBoehler/opinion-cpp-client.git
cd opinion-cpp-client
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/public_markets
```

CMake fetches nlohmann/json, IXWebSocket, and bitcoin-core secp256k1 (recovery module). Keccak-256 is in-tree. The library target is `opinion::client`.

`public_markets` calls the live host with no API key:

- `GET https://openapi.opinion.trade/openapi/market?limit=5&status=activated&marketType=0`
- `GET /market/{marketId}`
- `GET /token/orderbook?token_id=`
- `GET /token/latest-price?token_id=`

Public traffic is limited to **5 requests/second per IP** (`ratelimit-limit: 5` on the live response). Authenticated traffic is **15 requests/second per API key**. A 429 may include `Retry-After`.

## Environment

| Preset | REST base (paths below are relative to this) | WebSocket | Chain |
| --- | --- | --- | --- |
| `Environment::production()` | `https://openapi.opinion.trade/openapi` | `wss://ws.opinion.trade` | BNB Chain 56 |
| `Environment::proxy()` | `https://proxy.opinion.trade:8443/openapi` | same | 56 |

`production()` matches the TypeScript SDK `DEFAULT_API_HOST`. `proxy()` is the host in the Python configuration guide and the community Go client, with `/openapi` appended so both presets share the same relative paths (`/market`, `/order`, `/auth/api-key`).

### Two hosts

Python examples pass `host='https://proxy.opinion.trade:8443'` and the generated client requests `/openapi/market`. The TypeScript default host is already `https://openapi.opinion.trade/openapi`, and a live `GET /openapi/market` on that host returns HTTP 200. If you copy the Python host string into `Environment::rest_base` without `/openapi`, requests 404. `Environment::validate()` rejects a base that does not contain `/openapi`.

There is no public testnet in the docs. Chain id is **56** only. RPC stored on the preset is the documented public BSC endpoint `https://bsc-dataseed.binance.org` and is unused by the REST client.

Contract addresses from the TypeScript configuration page and `opinion_clob_sdk` 0.7.0 `config.py`:

| Contract | Address |
| --- | --- |
| ConditionalTokens | `0xAD1a38cEc043e70E83a3eC30443dB285ED10D774` |
| MultiSend | `0x38869bf66a61cF6bDB996A6aE40D5853Fd43B526` |
| FeeManager | `0xC9063Dc52dEEfb518E5b6634A6b8D624bc5d7c36` |

The older Python configuration **page** still prints a different MultiSend (`0x998739BFdAAdde7C933B942a68053933098f9EDa`). The wheel's `config.py` matches the TypeScript address above.

The CTF exchange used as EIP-712 `verifyingContract` is **not** a hardcoded preset. `place_order` reads `ctfExchangeAddress` from `GET /quoteToken` for the market's `quoteToken`. Do not copy a stale exchange constant from third-party order-utils; the live USDT quote token (`0x55d398326f99059fF775485246999027B3197955`) publishes its own exchange and `decimal` (18 on the live list).

## What v1 implements

Public, no `apikey`:

| Method | Path |
| --- | --- |
| GET | `/market` |
| GET | `/market/{marketId}` |
| GET | `/market/categorical/{marketId}` |
| GET | `/market/slug/{slug}` |
| GET | `/label` |
| GET | `/token/orderbook?token_id=` |
| GET | `/token/latest-price?token_id=` |
| GET | `/token/price-history` |
| GET | `/quoteToken` |

Authenticated with header `apikey` (the same key as OpenAPI, WebSocket, and the CLOB SDKs):

| Method | Path |
| --- | --- |
| GET | `/order` |
| GET | `/order/{orderId}` |
| POST | `/order` |
| POST | `/order/cancel` with `{"orderId":"..."}` |
| GET | `/positions/user/{walletAddress}` |

API-key mint, no `apikey` header. Headers `OPINION_ADDRESS`, `OPINION_SIGNATURE`, `OPINION_TIMESTAMP`:

| Method | Path | Signed `action` |
| --- | --- | --- |
| POST | `/auth/api-key` | `create` |
| GET | `/auth/api-key` | `get` |
| DELETE | `/auth/api-key` | `delete` |

Live responses use `errno` / `errmsg` / `result`. Some doc pages still show `code` / `msg`. The client accepts either and treats `0` as success. An invalid `apikey` is HTTP 401 with no anonymous fallback.

`marketType` on `GET /market` follows the current market page: `0` binary, `1` categorical, `2` all. The older generated client described those values differently.

## Auth

Wallet must already be a registered Opinion account with trading enabled. Each wallet has one active key. Sign this EIP-712 payload (domain has **no** `verifyingContract`):

```json
{
  "domain": { "name": "Opinion OpenAPI", "version": "1", "chainId": 56 },
  "primaryType": "OpinionApiKeyAuth",
  "message": { "walletAddress": "0x…", "action": "create", "timestamp": "1753690000" }
}
```

`OrderSigner::sign_api_key_auth` produces the three headers. Signatures expire after 5 minutes. `create` and `delete` are single-use. A new key can take about 15 seconds before the gateway accepts it; a deleted key can linger about 10 seconds.

```cpp
opinion::ClobClient client(opinion::Environment::production());
client.set_signer(std::make_unique<opinion::OrderSigner>(std::getenv("PRIVATE_KEY")));
const auto created = client.create_api_key();
if (created)
    client.set_api_key(created.value().api_key);
```

## Orders

`place_order` follows `opinion_clob_sdk` 0.7.0:

1. `GET /market/{id}` and `GET /quoteToken`.
2. Exactly one of `maker_amount_in_quote_token` or `maker_amount_in_base_token`. Market buys are quote-only. Market sells are base-only. Human amounts must be at least 1.
3. Limit price is `0.01`–`0.99` with at most 4 decimal places (the public order docs). The Python amount helper internally allows a wider band; this client keeps the documented band.
4. Amounts are scaled by the quote token `decimal`, then limit orders are adjusted the way SDK 0.7.0 `calculate_order_amounts` does: 4 significant digits, then an exact price fraction. `feeRateBps` is `"0"`, matching that SDK.
5. EIP-712 domain `OPINION CTF Exchange` / `1` / chain id 56 / `verifyingContract = ctfExchangeAddress`.
6. `signatureType` is `2` (`POLY_GNOSIS_SAFE`). `maker` is the Safe / multiSig. `signer` is the EOA from the private key. `taker` is the zero address.
7. `POST /order` with the camelCase body the Python `V2AddOrderReq` sends: `salt`, `topicId`, `maker`, `signer`, `taker`, `tokenId`, `makerAmount`, `takerAmount`, `expiration`, `nonce`, `feeRateBps`, `side`, `signatureType`, `signature`, `sign`, `contractAddress` (empty), `currencyAddress`, `price`, `tradingMethod`, `timestamp`, `safeRate`, `orderExpTime`.

`side` on the signed order is `0` buy / `1` sell. REST order records use `1` buy / `2` sell. `tradingMethod` is `1` market / `2` limit.

`postOnly` is sent only when you set it. The TypeScript order page and the OpenAPI `OrderData` schema describe it. `opinion_api` 0.4.0 `V2AddOrderReq` does not list the field.

`check_approval=true` returns an error. On-chain `enableTrading` (approvals, split, merge, redeem) is not in this v1.

`cancel_order` is `POST /order/cancel` with `{"orderId":"..."}`.

## WebSocket

```text
wss://ws.opinion.trade?apikey={API_KEY}
```

Heartbeat, about every 30 seconds: `{"action":"HEARTBEAT"}`.

```text
{"action":"SUBSCRIBE","channel":"market.depth.diff","marketId":1274}
{"action":"SUBSCRIBE","channel":"market.last.price","marketId":1274}
{"action":"SUBSCRIBE","channel":"market.last.trade","rootMarketId":61}
{"action":"UNSUBSCRIBE","channel":"market.last.price","marketId":1274}
```

Market channels: `market.depth.diff` (binary `marketId` only), `market.last.price`, `market.last.trade`. User channels: `trade.order.update`, `trade.record.new`. When `rootMarketId` is set, `marketId` is omitted. A matched trade on `market.last.trade` or `trade.order.update` is not an on-chain fill; use `trade.record.new` for that.

`examples/ws_subscribe` prints those frames and connects only if `OPINION_API_KEY` is set. `LocalOrderBook` applies `market.depth.diff` as the absolute size at a price (`"0"` removes the level).

## Compared with polymarket-cpp-client

| | Polymarket C++ client | This client |
| --- | --- | --- |
| Layout | `include/polymarket`, libcurl, IXWebSocket, secp256k1, EIP-712 | Same shape under `include/opinion` |
| Chain | Polygon 137 | BNB Chain 56 |
| Hosts | Gamma + CLOB + data API | One OpenAPI host, plus the proxy host above |
| REST auth | L1 EIP-712 derive, then HMAC `POLY_*` headers | Static `apikey`; EIP-712 only to mint the key |
| Order domain | CLOB V2 | `OPINION CTF Exchange` version `1`, classic CTF fields (`taker`, `nonce`, `feeRateBps`, `expiration`) |
| Wallet | EOA, proxy, Safe, 1271 | Safe-first (`POLY_GNOSIS_SAFE`), maker ≠ signer |
| WebSocket | Native CLOB market/user sockets | Native `wss://ws.opinion.trade` channel subscribe |
| On-chain | Split / merge / redeem, indexer | Not in v1. Contract addresses are on `Environment` for a later position client |

Roughly the HTTP client, `Result` errors, order-book holder, and WebSocket transport match the Poly client. Auth headers, order struct, hosts, and chain do not.

## Docs

- https://docs.opinion.trade/
- https://docs.opinion.trade/developer-guide/opinion-open-api/overview
- https://docs.opinion.trade/developer-guide/opinion-open-api/authentication
- https://docs.opinion.trade/developer-guide/opinion-open-api/market
- https://docs.opinion.trade/developer-guide/opinion-open-api/token
- https://docs.opinion.trade/developer-guide/opinion-open-api/order
- https://docs.opinion.trade/developer-guide/opinion-websocket/quickstart
- https://docs.opinion.trade/developer-guide/opinion-websocket/market-channels
- https://docs.opinion.trade/developer-guide/opinion-clob-typescript-sdk/getting-started/configuration
- https://docs.opinion.trade/developer-guide/opinion-clob-typescript-sdk/core-concepts/order
- https://docs.opinion.trade/developer-guide/opinion-clob-python-sdk/overview

No single downloadable OpenAPI file was published at the usual swagger paths when this client was written. The per-page snippets use server `https://openapi.opinion.trade/openapi`.

## License

MIT. See [LICENSE](LICENSE).
