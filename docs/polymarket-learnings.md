# Polymarket learnings applied to Opinion

I especially appreciate Bill's efficiency and performance improvements in
Polymarket #33. This port adapts those ideas to Opinion's existing API contract.

## Changes

- A second persistent HTTP handle serves `POST /order` and `POST /order/cancel`.
  It receives the same HTTP options. A blocked read no longer queues cancellation.
  Orders still serialize with other orders; auth changes must not overlap requests.
- REST envelopes are parsed once. Typed readers use the parsed result directly;
  public mutation `raw_result` and error diagnostics remain serialized strings.
- Decimal uint256 encoding uses one fixed 32-byte word instead of repeated division
  with temporary strings. Invariant typed-data hashes are cached and preimages reserved.
- Local snapshots are validated and sorted. Equivalent decimal prices update or remove
  the same level. Binary search locates updates; best-price reads access the front.
  Decimal comparison uses string views, preserving arbitrary decimal precision.
- Invalid REST book levels, malformed decimals, and duplicate local snapshot prices
  report errors before book state changes.
- IXWebSocket does not implement the exposed proxy/interface options. `connect()` now
  returns false and calls `on_error` for a configured route, including the process-wide
  route. It no longer silently starts a direct connection. Proxy support is not claimed.
- Failed libcurl global initialization now reports an exception.

Public method signatures and signature vectors remain unchanged. `ClobClient` gains
an internal HTTP handle, so rebuild consumers with the library. Public headers now
need nlohmann JSON's forward declarations; the CMake target propagates that dependency.

## Measurements

Apple Silicon, AppleClang 21, Release build, 10 October 2026. Before values for
signing/book lookup are one baseline run at `bfede94`; after values are medians of
three final runs. REST compares the previous parse/dump/parse pattern against a
single parse within the same executable. These are local CPU timings, not exchange
latency or order-fill benchmarks.

| Operation | Before | After |
| --- | ---: | ---: |
| uint256, 78 decimal digits | 4.34 us | 0.93 us |
| Sign order with long salt/token IDs | 24.24 us | 17.33 us |
| REST envelope, 100 levels | 57.57 us | 24.06 us |
| Best bid, 200 existing levels | 7.66 us | <0.01 us |

The very short best-bid timing approaches loop/timer overhead. Its reliable result
is the algorithmic change from scanning all levels to reading one sorted level.

```sh
cmake --build build --target opinion_hot_paths --parallel 2
./build/opinion_hot_paths
ctest --test-dir build --output-on-failure
```

## Validation

All three CTest targets pass in Release and Debug with AddressSanitizer and
UndefinedBehaviorSanitizer. The sanitizer build targets macOS 12. Existing auth
and order-signature vectors pass unchanged. Regression tests cover uint256 bounds,
equivalent prices, zero-size deletion, duplicate/malformed values, atomic rejection,
and explicit/default unsupported WebSocket routes.

A local HTTP fixture holds a read until cancellation arrives. Cancellation completes
while the read remains blocked, preserving its method, API key, body, and raw result.
Both envelope formats, API errors, invalid JSON, and invalid typed levels are checked.
The fixture redirects a non-const client after construction only inside the test;
production environment validation continues to require HTTPS. No live trade is sent.

## History review

Reviewed Polymarket history through `6a4ee0f` (PR #33), including earlier transport,
signing, book parsing, lifecycle, packaging, and CI changes. Sources:

| Source change | Lesson | Application here |
| --- | --- | --- |
| [#33 / 6a4ee0f](https://github.com/SebastianBoehler/polymarket-cpp-client/commit/6a4ee0f) | Reuse HTTP handles, enable compression, isolate orders, avoid redundant metadata reads. | Both clients already reuse handles and enable compression. Opinion now isolates orders. Limitless's session is explicitly single-threaded. |
| [#29 / 7c4ae7e](https://github.com/SebastianBoehler/polymarket-cpp-client/commit/7c4ae7e) | Avoid repeated JSON parsing, numeric allocations, and invariant hashing; enforce configured routes. | Opinion ports integer encoding, cached hashes, single REST parsing, and route rejection. Limitless removes the extra event dispatch copy. |
| [a3cd1c8](https://github.com/SebastianBoehler/polymarket-cpp-client/commit/a3cd1c8) | Validate before publishing state; reject invalid numbers and duplicates; stop workers before destroying their state. | Both books validate before mutation. Limitless also stops a replaced socket implementation before its callback state is destroyed. |
| [1945e73](https://github.com/SebastianBoehler/polymarket-cpp-client/commit/1945e73), [33db662](https://github.com/SebastianBoehler/polymarket-cpp-client/commit/33db662) | Keep signing arithmetic exact and obey venue-specific tick rules. | Both clients already use decimal strings for amounts. Opinion's price comparison stays exact. Limitless now checks integer accumulation before overflow. Polymarket tick rules are not copied. |
| [5d348c9](https://github.com/SebastianBoehler/polymarket-cpp-client/commit/5d348c9), [ccc5a0b](https://github.com/SebastianBoehler/polymarket-cpp-client/commit/ccc5a0b) | Auth identities and order types must match the venue's protocol. | Signing vectors remain unchanged. Limitless uses HMAC; Opinion uses its own API-key auth. No Polymarket auth or FAK serialization is transplanted. |
| [#31 / 8e674d8](https://github.com/SebastianBoehler/polymarket-cpp-client/commit/8e674d8) | Never automatically retry order mutations. | No automatic retries are added. Existing HTTP/API error metadata remains available. |
| [#32 / 151c440](https://github.com/SebastianBoehler/polymarket-cpp-client/commit/151c440), [#34](https://github.com/SebastianBoehler/polymarket-cpp-client/pull/34) | Settlement success needs final chain evidence, not an intermediate stream event. | These clients do not expose the same settlement helper or status contract. No fabricated counterpart is introduced. |
| [880e082](https://github.com/SebastianBoehler/polymarket-cpp-client/commit/880e082), [95969e8](https://github.com/SebastianBoehler/polymarket-cpp-client/commit/95969e8), [94a809a](https://github.com/SebastianBoehler/polymarket-cpp-client/commit/94a809a) | Namespace public headers, validate dependency graphs, and bound compiler parallelism. | Headers are already namespaced; existing CI already limits builds to two workers. Validation here covers source builds and public examples, not a relocatable installed SDK. |

Polymarket's Gamma pools, tick/negative-risk caches, approvals, oracle/indexer helpers,
preproduction deployment, and heartbeat statistics have no direct equivalent here.
Polymarket's macOS floating `from_chars` regression is avoided: these ports do not
use floating `from_chars` or convert exact order integers through `double`.

