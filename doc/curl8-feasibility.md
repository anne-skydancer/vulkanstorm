# libcurl 7.54.1 to curl 8: feasibility study

Status: study, 2026-11-04. No viewer code is changed by this document.
Base: `origin/vkstorm-devel` at `6430374102`; other lines compared at
release `e44270ccf4`, canary `037635069a`, rust `8bd1ccdebd`, aleph
`91c411e69a`; Rust core `vkstorm-core-rust` `main` at `08a5cbda92`.
Companion to the OpenSSL study (`doc/openssl3-impact.md`, PR #314), which
kept curl 7.54.1 and named it the main open risk.

Each finding is marked **verified** (read in the source or headers,
inspected, or run) or **inferred** (reasoned from documentation or from code
without running it). Line numbers are in the devel tree unless stated.

## Recommendation

**Go, but as a second step after OpenSSL 3.5, and only through a measured
performance gate.** curl 8 is the better long-term route: 7.54.1 has 88
published vulnerabilities, cannot follow OpenSSL past 3.5, and nobody
maintains it. The viewer's curl code needs only small changes, because
Firestorm already guards most of the version-sensitive calls. The cost is
not the API. It is that **the Second Life asset CDN speaks only HTTP/1.1**
(verified below), so curl 8 removes pipelining from texture, mesh and asset
traffic and HTTP/2 gives nothing back there. Concurrency has to be
recovered with more parallel HTTP/1.1 connections, and that has to be
measured against today's pipelined loads, on a high-latency path too,
before it ships.

Order:
1. Build OpenSSL 3.5 and curl 8.23 packages together (curl 8.18+ requires
   OpenSSL 3.0+, so curl 8 cannot ship before the OpenSSL move anyway).
   Keep the 7.54.1 rebuild against 3.5 from the OpenSSL study as the
   package the OpenSSL change ships with, so the credential-store risk and
   the network-performance risk land separately.
2. curl 8 on its own feature branch: compile fixes, replacement of
   pipelining by connection concurrency (and HTTP/2 multiplexing where a
   server offers h2), the same change in the Rust `vkstorm-http`.
3. Ship only when texture and mesh load times are no worse than 7.54.1
   pipelined (gate G3). If they are worse and cannot be tuned, keep
   7.54.1 on OpenSSL 3.5 until 2030 and revisit when LL enables HTTP/2 on
   the CDN.

Effort: **12 to 19 working days** on top of the OpenSSL work
([Effort](#effort)).

### Comparison

| | curl 7.54.1 on OpenSSL 3.5 | curl 8.23 on OpenSSL 3.5 |
|---|---|---|
| Security support | None upstream. 88 published vulnerabilities, 7 High ([vuln-7.54.1](https://curl.se/docs/vuln-7.54.1.html)); LL's 3p-curl builds every protocol (FTP, SMTP, TFTP, ...) and its recent commits are build fixes, not backports (verified) | Current release, fixes ship every ~8 weeks ([releases](https://curl.se/docs/releases.csv)); we can disable all non-HTTP protocols |
| Effort | 1 to 3 days (rebuild; more if it needs patches), part of the OpenSSL study | 12 to 19 days: package, ~15 viewer call sites, pipelining replacement in C++ and Rust, measurement |
| Risk | Unproven build against OpenSSL 3 (medium); no behaviour change | Texture/mesh load regression for distant users (high until measured); small API/behaviour changes (low) |
| Performance | Unchanged: 8 connections x depth 5 to the CDN | HTTP/1.1 only to the CDN: needs ~2 to 5x more connections to keep the same requests in flight; HTTP/2 helps only on hosts that offer h2 (web services, not the CDN) |
| Future | Dead end: uses the ENGINE API, so it cannot move to OpenSSL 4 (4.1.0 is out, 2026-10-23); no HTTP/3 | Builds against OpenSSL 4.x (curl CI already tracks 4.1); HTTP/3 possible via ngtcp2 if SL ever offers it; ready the day LL enables HTTP/2 on the CDN |
| Licence | curl licence (MIT-style), compatible | Same, plus nghttp2 (MIT); keep libidn2/libunistring out (see [Licences](#licences)) |

### Risks, most severe first

1. **Texture and mesh load speed regression (high until measured).**
   Pipelining is the viewer's main latency-hiding mechanism for the CDN:
   with defaults, 40 texture, 40 mesh and 60 asset requests are in flight
   over 8, 8 and 12 connections. curl 8 silently ignores
   `CURLMOPT_PIPELINING=1` (no error), so with unchanged code the same 40
   requests queue behind 8 connections. The CDN answers ALPN with
   `http/1.1` only (verified), so HTTP/2 cannot replace it. LL's own
   position is that removing pipelining without a compensating change
   regresses performance, and Cool VL Viewer's author reports a large
   difference for overseas users ([viewer#3035](https://github.com/secondlife/viewer/pull/3035),
   [viewer#870](https://github.com/secondlife/viewer/issues/870)).
   Nobody has published a measurement. Mitigation: raise per-host
   connections for the pipelined classes so in-flight counts match, and
   measure (gate G3), including with added latency.
2. **More connections to the CDN may hit server-side limits (medium,
   inferred).** Monty Linden says pipelining was tuned together with the
   CDN ([#3035](https://github.com/secondlife/viewer/pull/3035)). Going from
   8 to 24 to 40 connections per host per viewer could meet Akamai or
   origin rate limiting, or produce 503s that `Retry-After` handling then
   throttles. Mitigation: start at the smallest count that matches
   today's timings; watch 503/429 rates in the fetch statistics.
3. **Hidden queueing and spurious timeouts (medium, inferred).**
   llcorehttp hands curl `per-host x depth` handles for pipelined classes
   (`_httppolicy.cpp:251-256`). If curl can run fewer at once, the rest wait
   in curl's pending queue, invisible to llcorehttp, while their transfer
   timeout may already be counting. The "in flight" rule must be re-derived
   to match what curl really runs, and the doubled transfer timeout for
   pipelined classes (`_httpoprequest.cpp:761`) re-examined.
4. **The same change twice, C++ and Rust (medium, effort).** The rust line
   replaces llcorehttp with `vkstorm-http`, which ports pipelining one to
   one (`transport.rs:766-790`, `policy.rs:199-203`) and has a parity
   fixture that encodes it. Both must change in step, and the Rust
   `link-curl` tests do not run in CI (verified).
5. **Linux build breaks on deprecations (low, certain).** Linux builds use
   `-Werror` (`indra/cmake/00-Common.cmake:277-278`) and curl 7.87+ marks
   deprecated API with `__attribute__((deprecated))` for GCC 12.1+ and
   clang (verified in the 8.11.1 `curl.h:36-58`). Hits:
   `CURLINFO_SIZE_DOWNLOAD`/`SPEED_DOWNLOAD` (`_httpoprequest.cpp:279,281`)
   and `curl_formadd`/`CURLOPT_HTTPPOST` (`linux_crash_logger.cpp:105-138`).
   MSVC is unaffected (the macro is empty there).
6. **Small behaviour changes (low).** Accept-Encoding is no longer sent on
   GET/PUT (guard at `_httpoprequest.cpp:521-523`); certificate-error
   messages at login lose their specific text
   (`llxmlrpctransaction.cpp:428-436`); HTTP/2 is negotiated by default with
   hosts that offer it (web services, `map.secondlife.com`). Each is
   listed with its fix in [API changes](#3-api-and-behaviour-changes).
7. **OpenSim (low, inferred).** OpenSim grids serve plain HTTP/1.1 from
   `OSWebServer` (verified for a sample). Pipelining against OpenSim was
   switched off by Firestorm in 2016 and back on in 2020 (verified); the
   connection-concurrency replacement applies unchanged. Some grids'
   single-threaded HTTP servers may cope worse with more connections.

## 1. Pipelining dependence (verified unless marked)

### What enables it

- `httprequest.h:187-218` defines `PO_PIPELINING_DEPTH`. Depth 0 or 1:
  llcorehttp manages connections and caps in-flight requests at
  `PO_CONNECTION_LIMIT`. Depth 2 or more: libcurl manages connections, up
  to `PO_PER_HOST_CONNECTION_LIMIT`, then pipelines.
- `_httpinternal.h:148-154`: connection limit default 8 (1 to 256),
  pipelining default 0, max 20.
- `_httppolicy.cpp:251-256`: a pipelined class may have
  `perHost x depth` requests active; otherwise `connectionLimit`.
- `_httplibcurl.cpp:545-572` (`policyUpdated`), one multi handle per class:
  depth > 1 sets `CURLMOPT_PIPELINING=1`, `MAX_PIPELINE_LENGTH=depth`,
  `MAX_HOST_CONNECTIONS=perHost`, `MAX_TOTAL_CONNECTIONS=connLimit`;
  otherwise `PIPELINING=0`, `MAX_HOST_CONNECTIONS=0` (unlimited),
  `MAX_TOTAL_CONNECTIONS=connLimit`. Options are applied only when the
  class is idle, because toggling pipelining with requests outstanding
  corrupted memory in 7.37 (`:520-536`).
- `_httpoprequest.cpp:734-768`: pipelined classes double the transfer
  timeout (BUG-7698/7688/7694, CDN stalls) and request HTTP/2 only if the
  `VIEWERASSET` environment variable is set (a test hook; MAINT-7081,
  commits 586d697475 and ead19aa22c, 2017).
- Firestorm desync detector: `_httpoprequest.cpp:969-973` records
  `X-LL-URL`, and `_httplibcurl.cpp:412-462` compares it and `Content-Range`
  with the request; on a mismatch it returns a retryable 503 and turns
  pipelining off on every class for the session (commit 8b743a2522, 2016,
  "if some AV messes up http pipelining").
- Nothing sets `CURLPIPE_MULTIPLEX`, `CURLMOPT_MAX_CONCURRENT_STREAMS`,
  `CURLOPT_PIPEWAIT` or `CURLOPT_MAXCONNECTS` (searched all of `indra`).

### Settings and classes (`llappcorehttp.cpp`)

`HttpPipelining` (Boolean, default 1, `settings.xml:6193`, debug setting
only, read once at start since SL-2141) and `PIPELINING_DEPTH = 5`
(`llappcorehttp.cpp:56`). A class pipelines when both the setting and its
table entry say so (`:436-465`). Pipelined classes get
`PO_CONNECTION_LIMIT = 2 x setting` and `PO_PER_HOST_CONNECTION_LIMIT =
setting` (`:479-534`). No Firestorm settings file overrides these keys.

| Class | Traffic | Pipelined | Setting (default) | curl per-host / total | Requests in flight |
|---|---|---|---|---|---|
| AP_TEXTURE | textures, ViewerAsset cap to the CDN (`lltexturefetch.cpp:1401,2743`) | yes | TextureFetchConcurrency (0 = 8) | 8 / 16 | 40 |
| AP_MESH2 | mesh < 2 MB, ViewerAsset/GetMesh2 (`llmeshrepository.cpp:1539-1567`) | yes | Mesh2MaxConcurrentRequests (8) | 8 / 16 | 40 |
| AP_ASSET (Firestorm) | other assets via ViewerAsset (`llviewerassetstorage.cpp:603`) | yes | AssetFetchConcurrency (0 = 12) | 12 / 24 | 60 |
| AP_MESH1, AP_LARGE_MESH, AP_UPLOADS, AP_LONG_POLL, AP_INVENTORY, AP_MATERIALS, AP_AGENT, AP_DEFAULT | legacy mesh, large mesh, uploads, event poll, inventory fetch, materials, appearance, everything else including AIS (`llcoproceduremanager.cpp`) and all simulator capabilities | no | various | unlimited / setting | = connection limit |

Consumers that scale with pipelining:
- Texture fetch water marks 100/50 pipelined vs 40/20 not
  (`lltexturefetch.cpp:234-237`, chosen at `:3259-3268`).
- Mesh2 high water `clamp(scale x setting, 32, 100)` with scale
  `2 x depth = 10` pipelined vs 5 (`llmeshrepository.cpp:4621-4645`): 80 vs
  40 at defaults.

**So pipelining carries only GET traffic to the asset CDN: textures, small
mesh, and other assets.** Inventory, AIS, event poll, uploads and all
simulator capabilities are already one request per connection.

### What it buys

Documented (verified, commit messages in the viewer history):
- 5cca78e718 (2014, Monty Linden): "First HTTP pipelining viewer", depth 5
  for textures and GetMesh2, water marks tuned to pipelining.
- 17da4cf57a (2014): "I can easily drop the default connection count to
  '4' and still hit the throttles ... a depth of '5' seems generally
  healthy for mesh. I had one run of 52.6S with a theoretical minimum of
  51.2S." This is the only number in the history, and it is a throttle
  test, not pipelining on versus off.
- SH-4371 (2013): the inter-request latency fix saved 7 to 10 ms of 22 ms
  per request; "With pipelining, this kind of thing should become
  unnecessary."
- MAINT-6067 / MAINT-6137 (2016): pipelining disabled for a curl timeout
  bug, then re-enabled with curl 7.47.

Inferred: the gain is latency hiding. With 8 connections and depth 5 the
viewer keeps about 40 small GETs outstanding instead of 8, so on a
150 ms round trip it is limited by bandwidth rather than by
`8 / RTT` requests per second. The benefit grows with distance from the
nearest Akamai edge, which matches the overseas reports. External
statements (verified, quoted on the PRs): Monty Linden "fairly strongly"
expects a regression without compensation, but also that HTTP/2 "may not
yield any significant loading improvement over HTTP/1"
([#3035](https://github.com/secondlife/viewer/pull/3035),
[#3054](https://github.com/secondlife/viewer/pull/3054)).

## 2. What replaces it

### Endpoint HTTP/2 support (probed 2026-11-04)

One TLS handshake per host with `openssl s_client -alpn h2,http/1.1`
(OpenSSL 3.5.9), or one `HEAD` with curl 8.23. No login, no credentials.

| Endpoint | Used for | ALPN result | Status |
|---|---|---|---|
| `asset-cdn.glb.agni.lindenlab.com:443` (CNAME `a948.c.akamai.net`, Akamai) | textures, mesh, assets | **http/1.1**, TLS 1.3 | verified |
| `asset-cdn.glb.agni.lindenlab.com:80` | same; code comments name `cdn:80` (`llappcorehttp.h:70,85,114`) | plain HTTP/1.1 (`403` to a bare `HEAD /`) | verified; h2c not offered (inferred: Akamai does not do h2c) |
| `login.agni.lindenlab.com:443` | XML-RPC login | **http/1.1**, TLS 1.3 | verified |
| `secondlife.com`, `www.secondlife.com`, `my.secondlife.com` (web profiles), `world.secondlife.com`, `id.secondlife.com`, `marketplace.secondlife.com`, `api.secondlife.com`, `update.secondlife.com`, `viewer-splash.secondlife.com` | web, profiles, marketplace, update check | **h2** | verified |
| `map.secondlife.com`, `secondlife-maps-cdn.akamaized.net` | world map tiles | **h2** | verified |
| Simulator capability hosts (`simhost-*.agni.secondlife.io:12043`), AIS (a capability), event poll | caps, inventory | not reachable without a session | not probed; inferred HTTP/1.1 (simulator's own HTTP server) |
| OpenSim: `login.osgrid.org:80`, `grid.kitely.com:8002`, `grid.3rdrockgrid.com:8002` | login, grid services | plain HTTP/1.1 (`nginx`, `OSWebServer`) | verified |
| OpenSim: `hypergrid.org:8002`, `login.zetamex.com` | | no answer / name not found | not reachable |
| OpenSim websites `www.kitely.com`, `www.osgrid.org` | not viewer traffic | Cloudflare, h2 and h3 | verified, irrelevant |

The h2 hosts carry little viewer traffic through curl (most web pages go
through CEF). The traffic pipelining serves gets no HTTP/2 today. LL has
said HTTP/2 on the CDN is a future joint server and viewer project with no
schedule ([#870](https://github.com/secondlife/viewer/issues/870),
[#3035](https://github.com/secondlife/viewer/pull/3035) comments of
2025-02-18).

### Proposed replacement (inferred design, to be measured)

- **HTTP/1.1 connection concurrency for the pipelined classes.** Keep the
  llcorehttp "deep queue" notion but make it real: set
  `MAX_HOST_CONNECTIONS` to the number of requests llcorehttp lets run, so
  nothing waits invisibly in curl. Start with 16 to 24 per host for
  textures and mesh2 and 24 for assets, then tune by measurement towards
  the cheapest value that matches pipelined timings. Port 80 means no extra
  TLS handshakes per new connection (inferred from the `cdn:80` comments;
  confirm with a trace, gate G1).
- **HTTP/2 multiplexing where offered.** Set
  `CURLMOPT_PIPELINING=CURLPIPE_MULTIPLEX` (2), `CURLOPT_PIPEWAIT=1` and
  `CURLMOPT_MAX_CONCURRENT_STREAMS` (7.67+) to the depth, so the day the
  CDN offers h2 over TLS the same classes multiplex with no further change.
  Do not force `CURL_HTTP_VERSION_2_0` on `http:` URLs.
- Keep `HttpPipelining` as the user-facing switch, renamed internally to
  "deep queue": on = the higher concurrency, off = today's non-pipelined
  numbers.
- Remove or neutralise the X-LL-URL desync detector and the
  stall-on-option-change (both exist only for HTTP/1.1 pipelining).
- Texture and mesh water marks keep keying off `isPipelined()`.

## 3. API and behaviour changes

Checked against the curl 8.11.1 headers (verified) and curl's current
documentation ([symbols-in-versions](https://raw.githubusercontent.com/curl/curl/master/docs/libcurl/symbols-in-versions),
[DEPRECATE.md](https://raw.githubusercontent.com/curl/curl/master/docs/DEPRECATE.md)).
C++ is not typechecked by curl's setopt macros (they are C-only,
`curl.h:3249-3253`, verified).

### Call sites that need a change (C++, devel; release, canary and aleph identical)

| File:line | Use | curl 8 | Change |
|---|---|---|---|
| `llcorehttp/_httplibcurl.cpp:545-572` | `CURLMOPT_PIPELINING` 1/0, `MAX_PIPELINE_LENGTH` | `CURLPIPE_HTTP1` ignored since 7.62, removed 7.65; `MAX_PIPELINE_LENGTH` no-op; 0 also switches HTTP/2 multiplexing off | Section 2 replacement |
| `llcorehttp/_httppolicy.cpp:251-256` | active limit `perHost x depth` | | Re-derive from what curl will run |
| `llcorehttp/_httpoprequest.cpp:734-768` | doubled timeout; HTTP/2 under `VIEWERASSET` | | Re-examine timeout; replace test hook with `PIPEWAIT` |
| `llcorehttp/_httplibcurl.cpp:412-462`, `_httpoprequest.cpp:969-973` | X-LL-URL desync detector | moot | Remove or keep inert |
| `newview/llappcorehttp.cpp:56,288-295,436-534`; `lltexturefetch.cpp:3259-3268`; `llmeshrepository.cpp:4621-4645` | depth, `HttpPipelining`, 2x connection rule, water marks | | Re-tune |
| `llcorehttp/_httpoprequest.cpp:279,281` | `CURLINFO_SIZE_DOWNLOAD`, `SPEED_DOWNLOAD` (double) | deprecated 7.55, warning under GCC/clang `-Werror` | `*_DOWNLOAD_T` (`curl_off_t`); `TOTAL_TIME` is fine |
| `linux_crash_logger/linux_crash_logger.cpp:105-138` | `curl_formadd`, `curl_formfree`, `CURLOPT_HTTPPOST` | deprecated 7.56, warning under `-Werror` | Enable the `curl_mime_*` version already drafted at `:72-103` |
| `llcorehttp/_httpoprequest.cpp:521-523`, `httpcommon.cpp:294-297` | `CURLOPT_ENCODING ""` compiled out for curl >= 7.81 (Firestorm guard: AWS sends `Content-Encoding: binary/octet-stream`, which newer curl rejects) | GET/PUT stop sending Accept-Encoding | Decide: keep off (assets are already compressed) or set it per class for LLSD caps; inventory responses may grow |
| `llcorehttp/_httpoprequest.cpp:627` | `CURLOPT_ENCODING ""` on POST, unguarded | a bad `Content-Encoding` now fails with `CURLE_BAD_CONTENT_ENCODING` (61) (inferred) | Align with the decision above |
| `newview/llxmlrpctransaction.cpp:428-436` | `#if CURLE_SSL_PEER_CERTIFICATE != CURLE_SSL_CACERT` (enum names in `#if` evaluate to 0, so this case is already compiled out with 7.54.1) and `case CURLE_SSL_CACERT` guarded `< 7.81` | `CURLE_SSL_CACERT` and `CURLE_SSL_PEER_CERTIFICATE` are both macros for `CURLE_PEER_FAILED_VERIFICATION` (60) since 7.62 | Add `case CURLE_PEER_FAILED_VERIFICATION:` to the `ssl_connect_error` message, else certificate failures at login get the generic text |
| `newview/llappcorehttp.cpp:572,581` | viewer's `sslVerify` returns `CURLE_SSL_CACERT` or `CURLE_SSL_PEER_CERTIFICATE` | both 60 | none to compile; the two cases become indistinguishable to callers |
| `newview/llxmlrpclistener.cpp:130,140,306-309` | name table and `case CURLE_SSL_CACERT` | key 60 labelled "SSL_CACERT"; branch now taken for curl's own failures with empty certificate data | Tidy (optional) |
| `indra/cmake/CURL.cmake` | links `libcurl` + `ll::openssl ll::nghttp2 ll::zlib-ng` | a static curl 8 on Windows also needs `ws2_32 crypt32 bcrypt iphlpapi secur32 normaliz` (inferred) | Add system libraries |
| `httpcommon.cpp:238-246` | `CURLE_HTTP_POST_ERROR` in the retry list | now `CURLE_OBSOLETE34`, never returned | optional |
| `fscommon.cpp:127-129` | `curl_unescape` | still exported | optional `curl_easy_unescape` |

Unchanged and verified present in 8.x: every other easy option the viewer
sets (`IPRESOLVE_V4`, `NOSIGNAL`, `COOKIEFILE ""`, `SSL_CTX_FUNCTION`,
`TIMEVALUE_LARGE` already selected by a `>= 7.59` guard at
`_httpoprequest.cpp:570-575`, `DNS_CACHE_TIMEOUT`, `CAINFO`/`CAPATH`,
`TIMEOUT`/`CONNECTTIMEOUT`, `HTTPHEADER`, the debug callback), the multi
calls (`init`, `perform`, `info_read`, `add`/`remove_handle`),
`curl_getdate`, `curl_global_init(CURL_GLOBAL_ALL)` on the main thread
(`httpcommon.cpp:348`) and `curl_version`. Not used anywhere (searched):
`CURLOPT_PROGRESSFUNCTION`, `CURLOPT_DNS_USE_GLOBAL_CACHE`,
`CURLOPT_PROTOCOLS`, `RANDOM_FILE`, `SSLVERSION`, `curl_share_*`,
`curl_multi_wait`/`poll`.

### Defaults that change (inferred from curl documentation unless marked)

- **HTTP/2 over TLS by default** (7.62) when built with nghttp2. Hosts
  that offer h2 (the `*.secondlife.com` web services, the map CDN) switch
  to h2. The viewer's `Connection: keep-alive` / `Keep-alive: 300` headers
  (`_httpoprequest.cpp:677-678`) are hop-by-hop and dropped on h2.
- **Multiplexing on by default** for a multi handle, but the viewer sets
  `CURLMOPT_PIPELINING` explicitly, so it stays off until we set 2.
- **`Expect: 100-continue`**: the viewer already strips it on POST, PUT
  and PATCH (`_httpoprequest.cpp:635,652`, verified); no change.
- **Connection cache**: unchanged API; no `MAXCONNECTS` is set.
- **Redirect protocols**: 7.54.1 follows redirects to any compiled-in
  protocol except FILE and SCP; LL's build compiles in FTP, TFTP, SMTP and
  others (verified: `build-cmd.sh` disables none). curl 8 restricts
  redirects to HTTP, HTTPS, FTP and FTPS by default (since 7.65.2,
  [CURLOPT_REDIR_PROTOCOLS](https://raw.githubusercontent.com/curl/curl/master/docs/libcurl/opts/CURLOPT_REDIR_PROTOCOLS.md),
  verified), and our build would disable all but HTTP(S) outright.
- **Common Name matching** is being removed for OpenSSL in the release
  after 8.23 ([DEPRECATE.md](https://raw.githubusercontent.com/curl/curl/master/docs/DEPRECATE.md));
  SL certificates use SANs; an OpenSim grid with a CN-only certificate
  would fail (inferred, test).
- **HTTP/2 server push** removal is planned for 2027; the viewer does not
  use it.

### Rust core (`vkstorm-core-rust`, `main` at `08a5cbda92`, verified)

No curl crate; hand-written `extern "C"` bindings to the viewer's libcurl
in `vkstorm-http/src/sys.rs:169-189` and a private copy in
`vkstorm-audio/src/stream/curl.rs:94-105`, constants hard-coded.

- **Symbols:** `curl_global_init`, `curl_version`, `curl_easy_{init,
  duphandle, reset, cleanup, setopt, getinfo, strerror, perform}`,
  `curl_multi_{init, cleanup, add_handle, remove_handle, perform,
  info_read, setopt, strerror}`, `curl_slist_{append, free_all}`. All
  present in curl 8 with the same signatures.
- **Constants:** 41 `CURLOPT_*`, 4 `CURLMOPT_*`, 8 `CURLINFO_*`, 13
  `CURLE_*`, plus enum values; every value matches curl 8. Long options
  pass `c_long`, which is right on both LLP64 and LP64. No `CURLE_SSL_CACERT`
  use. Audio uses `XFERINFOFUNCTION`, not the deprecated progress callback,
  and `HTTP200ALIASES` for ICY.
- **Must change:** `transport.rs:766-790` (pipelining options, as C++),
  `transport.rs:484-493` (doubled timeout), `policy.rs:199-203` (active
  limit), `transport.rs:650-705` (desync detector); add constants for
  `CURLPIPE_MULTIPLEX`, `CURLMOPT_MAX_CONCURRENT_STREAMS`,
  `CURLOPT_PIPEWAIT`. The rust line's parity fixture
  (`tests/fixtures/llcorehttp_parity.xml`) and tests encode pipelined
  behaviour and need re-baselining.
- **Optional:** `CURLINFO_*_DOWNLOAD` doubles to `_T` (`transport.rs:636-647`);
  `CURLOPT_TIMEVALUE` truncates to 32 bits on Windows
  (`transport.rs:405`) where `TIMEVALUE_LARGE` is now available; update the
  "7.54.1" comments and the README link list.
- **Testing gap:** the `link-curl` suites are not run in CI
  (`.github/workflows/test.yaml:60-63`); run them by hand against the new
  library.

### Per-line differences (verified)

| Line | curl usage vs devel | Notes |
|---|---|---|
| vkstorm-release | identical | same patch applies |
| vkstorm-canary | identical curl calls; `httpcommon.*` constants made `constexpr`, thread model changes in `_httpservice.cpp` | same patch, small context differences |
| vkstorm-aleph | as canary | same |
| vkstorm-rust | C++ llcorehttp internals deleted; `httpcommon.*`, `llproxy.cpp`, the `SSL_CTX` hook (`httprequest.cpp`), `llxmlrpc*`, crash logger remain; `llappcorehttp.cpp` unchanged | C++ parts as above, plus the Rust changes |

All five lines pin the same packages: curl `7.54.1-r4`
(`secondlife/3p-curl`), nghttp2 `1.64.0-r1`, OpenSSL `1.1.1w-r4`,
zlib-ng 2.2.5; no c-ares.

## 4. TLS backend

**Recommend OpenSSL 3.5 on both platforms.**

- `CURLOPT_SSL_CTX_FUNCTION` works only with OpenSSL, wolfSSL and mbedTLS;
  it is **not called with Schannel** ([docs](https://raw.githubusercontent.com/curl/curl/master/docs/libcurl/opts/CURLOPT_SSL_CTX_FUNCTION.md),
  verified). The viewer's own certificate validation
  (`_httpoprequest.cpp:539-540`, callback `:1038-1062`;
  `llappcorehttp.cpp` `sslVerify`; the rust line's hook in
  `httprequest.cpp`) installs `SSL_CTX_set_cert_verify_callback` through
  it. With Schannel the viewer would silently fall back to curl's own
  verification against the Windows store, the `NoVerifySSLCert` and
  certificate-store logic would stop applying, and Windows and Linux would
  behave differently.
- OpenSSL is needed in the viewer anyway (credential store, `llsecapi`,
  certificate handling), so Schannel saves nothing.
- One backend on both platforms keeps OpenSim certificate behaviour the
  same everywhere.
- If the OS root store is wanted later, `CURLSSLOPT_NATIVE_CA` (7.71) adds
  it under OpenSSL without changing backend.

Building curl 8 locally to confirm the hook was not needed: the behaviour
is documented, unchanged since 7.10.6, and the viewer already uses the
OpenSSL backend.

## 5. Packages

| Source | curl | OpenSSL | Platforms | Usable? |
|---|---|---|---|---|
| secondlife/3p-curl | 7.54.1-r4 latest (2026-01-14); no curl 8 work | 1.1.1w | all | no ([repo](https://github.com/secondlife/3p-curl)) |
| FirestormViewer phoenix-firestorm master | uses LL 7.54.1-r4; Firestorm's 3p-curl fork is stale (2023) | 1.1.1w | | no |
| Alchemy (vcpkg, `AlchemyViewer/alchemy-registry`) | still **7.54.1** with a pipelining patch | not checked | | no ([port](https://github.com/AlchemyViewer/alchemy-registry/tree/main/ports/curl)) |
| Megapahit | LL 7.54.1-r4 | 1.1.1w | | no |
| AiraYumi/3p-curl, branch `update` | **8.17.0-r3** (2025-11-16) | AiraYumi 3p-openssl 3.5.4 | windows64, linux64, darwin64 | not for shipping: one maintainer, four releases behind, no licence metadata, brotli/zstd switches partly mistyped (`CURL_ZTSD`) ([repo](https://github.com/AiraYumi/3p-curl)) |

So nobody publishes a usable curl 8 + OpenSSL 3 autobuild package
(verified). **Build our own**, alongside the OpenSSL 3.5 package from the
OpenSSL study: fork `secondlife/3p-curl` under `anne-skydancer`, replace
the source with curl 8.23.0 (current, 2026-10-14, verified with the local
curl 8.23 binary), and adapt `build-cmd.sh`. Current CMake options
(verified in curl master `CMakeLists.txt`):

- `BUILD_SHARED_LIBS=OFF`, `CURL_STATICLIB`, `CURL_USE_OPENSSL=ON` with the
  OpenSSL 3.5 package (`OPENSSL_USE_STATIC_LIBS=ON`), `USE_NGHTTP2=ON` with
  the 3p-nghttp2 package (`NGHTTP2_STATICLIB`), zlib-ng as zlib.
- `ENABLE_THREADED_RESOLVER=ON`, `ENABLE_ARES=OFF` (as today).
- Off: `USE_LIBIDN2`, `CURL_USE_LIBPSL` (both default **on** in current
  curl), `CURL_BROTLI`, `CURL_ZSTD`, `CURL_USE_LIBSSH2`, LDAP/LDAPS,
  GSSAPI, `BUILD_TESTING`. Disable every protocol except HTTP(S) with the
  individual `CURL_DISABLE_*` options (the old `HTTP_ONLY` switch is gone).
  Optional: `USE_WIN32_IDN=ON` on Windows.
- Match the viewer's CRT (`/MD`) and LTO.
- c-ares, PSL, brotli, zstd: not needed. The viewer sets its own cookies
  only within SL services, SL hosts are ASCII, and the CDN serves
  already-compressed assets.

Effort for the package: 2 to 3 days for windows64 and linux64, including
a `curl --version` smoke test and the curl test suite's HTTP and TLS tests
(inferred). darwin64 as in the OpenSSL study: only if a macOS build is
revived.

## Licences

The viewer is becoming dual LGPL-2.1 / GPL-3.0 (PR #306).

| Component | SPDX | Compatible with both? | Source |
|---|---|---|---|
| curl | curl (MIT-style) | yes | [copyright](https://curl.se/docs/copyright.html) |
| nghttp2 | MIT | yes | [repo](https://github.com/nghttp2/nghttp2) |
| zlib-ng | Zlib | yes | [repo](https://github.com/zlib-ng/zlib-ng) |
| c-ares (not planned) | MIT | yes | [repo](https://github.com/c-ares/c-ares) |
| libpsl (not planned) | MIT, but pulls libidn2/libunistring or ICU | see below | [repo](https://github.com/rockdaboot/libpsl) (inferred) |
| brotli (not planned) | MIT | yes | [repo](https://github.com/google/brotli) |
| zstd (not planned) | BSD-3-Clause OR GPL-2.0-only | yes, under BSD-3 | [repo](https://github.com/facebook/zstd) |
| libssh2 (not planned) | BSD-3-Clause | yes | [repo](https://github.com/libssh2/libssh2) |
| ngtcp2, nghttp3 (HTTP/3, not planned) | MIT | yes | [ngtcp2](https://github.com/ngtcp2/ngtcp2) |
| libidn2, libunistring | GPL-2.0-or-later OR LGPL-3.0-or-later | **avoid**: neither option is compatible with an LGPL-2.1-only grant; it works only through the GPL-3 side | [libidn2 README](https://gitlab.com/libidn/libidn2/-/raw/master/README.md) |
| OpenSSL 3.5 | Apache-2.0 | GPL-3 yes; LGPL-2.1 only with "or later" | covered by the OpenSSL study |

With the build above (OpenSSL, nghttp2, zlib-ng only), curl 8 adds no
licence question beyond the OpenSSL one already recorded.

## 6. Test plan

### Gates

- **G0 packages:** curl 8.23 static libraries for windows64 and linux64 on
  OpenSSL 3.5; `curl --version` shows `OpenSSL/3.5.x nghttp2` and only
  HTTP(S); curl's HTTP and TLS tests pass.
- **G1 baseline (before any change):** with the 7.54.1 viewer, record a
  network trace of one session to confirm the ViewerAsset URL scheme and
  port (http:80 or https:443), and the connection count the viewer opens
  to the CDN.
- **G2 build:** Windows `/WX` and Linux `-Werror` RelWithDebInfo builds
  clean; `llcorehttp` unit tests and the integration tests pass; on the
  rust line the parity tests and the `link-curl` suites pass.
- **G3 performance (blocking):** curl 8 with the new concurrency must match
  7.54.1 pipelined within noise on texture and mesh load time, for each
  of: local latency, +150 ms added latency (Windows `clumsy` or Linux
  `tc netem`), and one OpenSim grid. No rise in 503/429 or timeouts.
- **G4 functional:** login (SL and OpenSim; HTTPS and HTTP login URIs),
  certificate errors show the right message, HTTP proxy and SOCKS5 proxy
  from Preferences (`llproxy.cpp:431-465`), inventory/AIS, uploads, event
  poll survives long idles, Linux crash logger upload (`curl_mime_*`),
  parcel audio streams (vkstorm-audio on the rust line). Media/SLPlugin
  and voice do not use the viewer's curl (CEF and the voice SDK bring
  their own) and need only a smoke test.
- **G5 in-world:** the owner's single in-world check after release.

### Measuring load performance

- `indra/llcorehttp/examples/http_texture_load.cpp` (verified present): a
  stand-alone loader that takes a URL list and a pipelining depth. Build it
  against both curls and replay the same texture URL list from a G1 trace
  (no credentials needed for CDN asset URLs, inferred). This isolates the
  HTTP layer from rendering.
- `--logmetrics TextureFetchTester` (`lltexturefetch.cpp:80,2752`) writes
  the viewer's own texture fetch metrics; the texture and mesh fetch
  statistics in the Statistics floater and fast-timer logs give totals.
- `scripts/perf/perfbot_run.py` runs non-interactive viewers; with a fixed
  start location and cleared cache it gives a repeatable "time to all
  textures loaded" run. `profile_cmp.py` compares the results.
- Run each configuration at least five times with a cleared cache; report
  median and spread.

## Effort

| Stage | Days |
|---|---|
| 0. curl 8.23 package (with the OpenSSL 3.5 package from the OpenSSL study) | 2 to 3 |
| 1. G1 baseline trace and measurement harness | 1 to 2 |
| 2. Compile fixes: deprecations, crash logger mime, `CURL.cmake` system libs, error mapping, Accept-Encoding decision | 1 to 2 |
| 3. Pipelining replacement in llcorehttp and llappcorehttp, water marks | 2 to 3 |
| 4. Same in Rust `vkstorm-http`, parity fixture re-baseline | 2 to 3 |
| 5. G3 measurements and tuning | 3 to 4 |
| 6. G4 functional checks, per-line integration PRs | 1 to 2 |
| **Total** | **12 to 19** |

This comes after the OpenSSL 3.5 change (8 to 12 days in that study). If
the 7.54.1 rebuild against OpenSSL 3.5 fails, stages 0 to 3 and 5 move
forward to become part of the OpenSSL work, and the two risks land
together.

## Sources

- Viewer history: commits 5cca78e718, 17da4cf57a, e79a88c8cc, de9689e39b,
  70d4c0ad7d, bfabb7bd2b, 586d697475, ead19aa22c, 766a9f150c, 8b743a2522,
  28fa0c0468, b113c6cc88 (`git show` in this repository).
- curl: [releases.csv](https://curl.se/docs/releases.csv),
  [vuln-7.54.1](https://curl.se/docs/vuln-7.54.1.html),
  [CURLMOPT_PIPELINING](https://raw.githubusercontent.com/curl/curl/master/docs/libcurl/opts/CURLMOPT_PIPELINING.md),
  [CURLMOPT_MAX_CONCURRENT_STREAMS](https://raw.githubusercontent.com/curl/curl/master/docs/libcurl/opts/CURLMOPT_MAX_CONCURRENT_STREAMS.md),
  [CURLOPT_SSL_CTX_FUNCTION](https://raw.githubusercontent.com/curl/curl/master/docs/libcurl/opts/CURLOPT_SSL_CTX_FUNCTION.md),
  [DEPRECATE.md](https://raw.githubusercontent.com/curl/curl/master/docs/DEPRECATE.md),
  [DEPENDENCIES.md](https://raw.githubusercontent.com/curl/curl/master/docs/DEPENDENCIES.md),
  [HTTP3.md](https://raw.githubusercontent.com/curl/curl/master/docs/HTTP3.md),
  [libcurl-thread](https://raw.githubusercontent.com/curl/curl/master/docs/libcurl/libcurl-thread.md),
  curl 8.11.1 `include/curl/curl.h` and `multi.h`.
- Linden Lab: [viewer#870](https://github.com/secondlife/viewer/issues/870),
  [#3035](https://github.com/secondlife/viewer/pull/3035),
  [#3054](https://github.com/secondlife/viewer/pull/3054),
  [#3101](https://github.com/secondlife/viewer/pull/3101),
  [#3163](https://github.com/secondlife/viewer/issues/3163),
  [3p-curl](https://github.com/secondlife/3p-curl).
- Other viewers (design and experience only, no code used):
  [Alchemy registry](https://github.com/AlchemyViewer/alchemy-registry),
  [AiraYumi/3p-curl](https://github.com/AiraYumi/3p-curl),
  [Firestorm autobuild.xml](https://raw.githubusercontent.com/FirestormViewer/phoenix-firestorm/master/autobuild.xml).
- OpenSSL 4 support in curl: [curl#23215](https://github.com/curl/curl/pull/23215).
