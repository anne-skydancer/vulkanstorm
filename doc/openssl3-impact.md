# OpenSSL 1.1.1 to OpenSSL 3: impact study

Status: study, 2026-11-04. No viewer code is changed by this document.
Base: `origin/vkstorm-devel` at `61f2a8f7bf`; other lines compared at
release `f2b005fd56`, canary `ffe865432a`, rust `f62f2d0294`,
aleph `7f9e7e2002`.

Each finding is marked **verified** (read in the source or headers,
inspected in a build, or run) or **inferred** (reasoned from documentation
or from code without running it).

## Recommendation

**Do it, on OpenSSL 3.5 LTS, as a dependency change kept separate from any
libcurl upgrade.** OpenSSL 3.5 is supported until 2030-04-08; 1.1.1 has been
end of life since September 2023 and 3.0 is now end of life too
([release strategy](https://openssl-library.org/policies/releasestrat/)).
OpenSSL 3.x is Apache-2.0, which removes the GPL-3.0 blocker recorded in
`doc/licensing.md` (PR #306) for the binaries.

How: build our own autobuild packages (nobody publishes one: see
[Package availability](#package-availability)): OpenSSL 3.5.x configured
`no-shared no-module`, so it stays statically linked and the legacy provider
is compiled into `libcrypto` instead of being a `legacy.dll`/`legacy.so` we
would have to stage. Rebuild the existing libcurl 7.54.1 against it, keeping
HTTP pipelining and the Rust HTTP port's behaviour unchanged. Change four
viewer files, the largest about 80 lines. The one piece that must not go
wrong is the credential store (`bin_conf.dat`), which is RC4-encrypted: RC4
is only in the legacy provider in 3.x. Load the legacy provider into a
private library context used only for that file, check every EVP return
value, and gate the change on a test that reads a store written by the
current 1.1.1 viewer.

When: after the GLOD removal lands, as a feature branch from `vkstorm-devel`
(packages first, then the viewer change), then one integration PR per line.
Estimated effort is 8 to 12 working days ([Effort](#effort)). It does not
need to wait for LL or Firestorm; neither has moved (verified, see below).

### Risks, most severe first

1. **Saved credentials lost (high, avoidable).** `llsechandler_basic.cpp`
   encrypts `bin_conf.dat` with `EVP_rc4()` and ignores every EVP return
   value. With OpenSSL 3 and no legacy provider the cipher fetch fails
   (verified with the OpenSSL 3.5.9 CLI, below). Decryption then yields
   garbage, the viewer shows `CorruptedProtectedDataStore`, and the next
   save replaces the file. The write path would also write
   `encrypted_length` bytes from an uninitialised variable. Every
   remembered login, MFA token and saved grid credential would be lost.
   Mitigation: RC4 through the legacy provider in a private
   `OSSL_LIB_CTX`, return values checked, a compatibility test as a gate.
2. **libcurl 7.54.1 against OpenSSL 3 is unproven here (medium).** The
   viewer depends on LL's pipelining-capable curl 7.54.1; newer curl (7.62
   and later) dropped HTTP/1.1 pipelining. Its OpenSSL backend guards its
   1.0-era calls by version and the ENGINE API still exists in 3.5
   (verified by reading `lib/vtls/openssl.c` in secondlife/3p-curl).
   That ENGINE use also means curl 7.54.1 cannot follow to OpenSSL 4.x,
   which removed engines; 3.5 LTS is the last line it can use.
   A community build of 7.54.1 against OpenSSL 3.4.0 was used in
   [secondlife/viewer#3112](https://github.com/secondlife/viewer/pull/3112)
   (inferred: it compiled; the PR's CI failed on an unrelated macOS
   test). If it does not build, the fallback is curl 8.x, which is a
   behavioural change of its own (pipelining becomes HTTP/2 multiplexing,
   and `CURLE_SSL_CACERT` and `CURLE_SSL_PEER_CERTIFICATE` merge into one
   code, which `llxmlrpctransaction.cpp:428` relies on).
3. **Older HTTPS servers refused (medium-low, mostly OpenSim).** OpenSSL 3
   rejects TLS 1.0/1.1 at the default security level, requires RFC 5746
   secure renegotiation, and since 3.2 defaults to security level 2 (no
   RSA/DH keys under 2048 bits). Second Life endpoints are unaffected:
   all probed negotiate TLS 1.3 or 1.2 with secure renegotiation and
   2048-bit or EC keys (verified). Most OpenSim login URIs are plain HTTP;
   the HTTPS ones probed negotiate TLS 1.3 (verified for a sample only).
   An old self-hosted grid behind an old TLS stack could fail. SL
   simulator capability hosts (port 12043) could not be probed without
   a session; the in-world gate covers them.
4. **Build breaks on deprecations (low, certain).** Windows builds use
   `/WX` and Linux `-Werror` (`indra/cmake/00-Common.cmake:149`, `:278`).
   The `ENGINE_*` calls in `llsecapi.cpp` are `OSSL_DEPRECATEDIN_3_0`, so
   they will fail the build until they are removed (planned anyway).
5. **Performance (low).** OpenSSL 3.0 had known regressions in handshakes
   and lock contention, which later releases largely addressed (inferred
   from release notes, not measured here). The viewer
   opens many concurrent TLS connections to the asset CDN. Measure it at
   a gate rather than assume it.

**Saved credentials survive the switch** if, and only if, RC4 remains
available to `llsechandler_basic.cpp`. The on-disk format does not depend on
OpenSSL's version: a 16-byte salt XORed with the machine ID, then the LLSD
XML encrypted with plain RC4 keyed by the salt. Verified: the store blob in
`indra/newview/tests/llsechandler_basic_test.cpp` (test 3) decrypts to the
expected LLSD with a textbook RC4, and the OpenSSL 3.5.9 legacy provider
gives byte-identical output, while OpenSSL 3.5.9 with only the default
provider refuses (`Error setting cipher RC4 ... unsupported`). No migration
step is needed if RC4 stays readable. Moving to a stronger format, which
Alchemy has done (ChaCha20 with an RC4 read fallback; design reference only),
is a separate decision: a downgraded viewer could not read the new file.

## 1. Where OpenSSL is used

### Linkage (verified)

- `indra/cmake/OpenSSL.cmake` finds `libssl.lib`/`libssl.a` and
  `libcrypto.lib`/`libcrypto.a` in the prebuilt directory and links them
  **statically**; Windows adds `Crypt32.lib`. `indra/cmake/CURL.cmake`
  links libcurl with `ll::openssl`, nghttp2 and zlib-ng.
- In a staged RelWithDebInfo build
  (`vkr/inv-r2/build-vc180-64/newview/RelWithDebInfo`): `libcrypto.lib` is
  26 MB and `libssl.lib` 6 MB (static archives). `dumpbin /dependents
  vulkanstorm-bin.exe` lists no OpenSSL DLL, and no `*ssl*`/`*crypto*` DLL
  is staged anywhere in the tree. `viewer_manifest.py` has the old
  `ssleay32`/`libeay32` and Linux `libcrypto.so*`/`libssl.so*` lines
  commented out (`:815`, `:2481`).
- Libraries in `packages/lib/release` that contain OpenSSL API names:
  `libcrypto.lib`, `libssl.lib`, `libcurl.lib` (our OpenSSL), and
  `webrtc.lib` (its own BoringSSL). `aprutil-1.lib` and
  `libcollada14dom23-s.lib` do not reference OpenSSL. There is no
  xmlrpc-epi or libssh2 in the package set; Firestorm 7.2.5 has its own
  XML-RPC over llcorehttp.

### Viewer source (verified, `grep` for includes and API names)

| File | What it does with OpenSSL |
|---|---|
| `indra/newview/llsecapi.cpp:45-63` | Init: `ERR_load_crypto_strings`, `OpenSSL_add_all_algorithms`, loads the `rdrand` ENGINE as the default RAND (`<FS:ND>`) |
| `indra/newview/llsechandler_basic.cpp` | Certificate objects and store (`X509`, `PEM_*`, `BIO_*`, `ASN1_*`, `X509_NAME_*`, `X509_get_ext_d2i`), the viewer's own chain validation (`X509_verify`, `:999`), chain from `X509_STORE_CTX` (`:699-716`), the protected data store (`EVP_rc4`, `:1365`, `:1447`; `RAND_bytes` salt, `:1431`) |
| `indra/newview/llsecapi.h`, `llsechandler_basic.h` | `X509*`/`X509_STORE_CTX*` in interfaces |
| `indra/llcorehttp/_httpoprequest.cpp:1045-1076` (`.h:36-37`) | libcurl `CURLOPT_SSL_CTX_FUNCTION`: `SSL_CTX_set_verify`, `SSL_CTX_set_cert_verify_callback` |
| `indra/newview/llappcorehttp.cpp:205`, `:538-575` | Registers `sslVerify` as the global `PO_SSL_VERIFY_CALLBACK`; validates the chain with `LLBasicCertificateStore::validate` |
| `indra/newview/llxmlrpctransaction.cpp` | Includes `ssl.h`/`x509_vfy.h`; maps `CURLE_SSL_*` codes |
| `indra/newview/exoflickr.cpp:272` | OAuth signature: one-shot `HMAC(EVP_sha1(), ...)` |
| `indra/newview/llscripteditorws.cpp:3023` | `RAND_bytes` for a WebSocket challenge |
| `indra/llcrashlogger/llcrashlogger.cpp:771-818` | 1.0-style `CRYPTO_num_locks`/`CRYPTO_set_locking_callback` (no-op macros since 1.1.0; built only when `USE_BUGSPLAT` is off) |
| `indra/llcorehttp/tests/llcorehttp_test.h`, `examples/http_texture_load.cpp` | Include `crypto.h`, no calls |
| `indra/newview/tests/llsechandler_basic_test.cpp` | Certificate and protected-store tests (test 3 holds an RC4 store blob) |

`llmd5`, `llmessage` and `llfilesystem` do not use OpenSSL (MD5 is LL's own
`llcommon/llmd5.cpp`). `llappviewer.cpp` only calls `initializeSecHandler()`
(`:1364`) and shows `CorruptedProtectedDataStore` when it throws.

**Certificate verification path (verified):** every llcorehttp request gets
the viewer's callback through `SSL_CTX_set_cert_verify_callback`. In OpenSSL
3.5, `ssl_verify_cert_chain` (`ssl/ssl_cert.c:504`) calls the application
callback **instead of** `X509_verify_cert`, so OpenSSL's security-level
checks on the certificate chain (key size, SHA-1 signatures) do not run for
the viewer. `LLBasicCertificateStore::validate` decides, and its
`X509_verify` uses the default provider, which still has SHA-1 and MD5.
OpenSSL 3's chain rules therefore do not change which certificates the viewer
accepts. Handshake-level rules (protocol version, handshake signature
algorithms, DH size, renegotiation) do still apply.

### Other components in or beside the process

| Component | TLS library | Effect of the switch |
|---|---|---|
| libcurl 7.54.1 (3p-curl) | our OpenSSL, static | must be rebuilt against 3.5 (same ABI requirement as today) |
| WebRTC voice (`llwebrtc.dll`/`.so`) | BoringSSL inside `webrtc.lib` | Windows: a separate DLL whose imports and exports contain no SSL symbols (verified with `dumpbin`), so no clash. Linux: built with `-fvisibility=hidden` and `-Wl,--exclude-libs,ALL` (`00-Common.cmake:204`, `:237-238`), so neither copy exports its symbols (inferred no clash). Same situation as today |
| CEF/Dullahan | BoringSSL inside `libcef` | separate process (`dullahan_host.exe` under SLPlugin) |
| VLC (media, speaker-fill) | VLC's GnuTLS modules | separate SLPlugin process |
| SLVoice (Vivox) | its own | separate process; OpenSim builds only |
| FFmpeg (`avcodec`/`avutil`/`swresample`) | none (no `avformat` staged) | none |
| Linux `wrapper.sh:146-155` | `LD_PRELOAD`s `lib/libcrypto.so.1.0.0` if present | dead code (nothing is staged); remove when convenient |
| Windows Inno installer | no OpenSSL files | no change while static |
| Rust core (`vkstorm-core-rust`) | none: `Cargo.lock` has no openssl, rustls, native-tls, ring or curl crate (verified). `vkstorm-http` and `vkstorm-audio` call the viewer's libcurl through its symbols | no Rust change. The rust line's `indra/llcorehttp/httprequest.cpp:172-186` has the same `SSL_CTX` hook as the C++ path |

## 2. API impact

Headers checked at tag `openssl-3.5.4`.

| Call (file:line) | 3.x status | Replacement | Size |
|---|---|---|---|
| `ENGINE_load_builtin_engines`, `ENGINE_by_id("rdrand")`, `ENGINE_set_default` (`llsecapi.cpp:49-53`) | `OSSL_DEPRECATEDIN_3_0`: a build error under `/WX`/`-Werror`; engines are removed in 4.0 (`CHANGES.md` at `openssl-4.0.0`) | Delete. The 3.x DRBG seeds from the OS and uses RDRAND/RDSEED as an entropy source where present; feeding raw RDRAND output as the RAND method was never better | -15 lines |
| `ERR_load_crypto_strings`, `OpenSSL_add_all_algorithms` (`llsecapi.cpp:45-46`) | macros deprecated since 1.1.0; init is automatic | Delete, or one `OPENSSL_init_crypto(OPENSSL_INIT_LOAD_CONFIG, nullptr)` if a config file is ever wanted | -2 |
| `EVP_rc4()` + `EVP_DecryptInit`/`EVP_EncryptInit`/`*Update` (`llsechandler_basic.cpp:1362-1380`, `:1440-1468`) | RC4 lives in the legacy provider only; unchecked returns | `OSSL_LIB_CTX_new()` + `OSSL_PROVIDER_load(ctx, "legacy")` + `EVP_CIPHER_fetch(ctx, "RC4", nullptr)` + `EVP_CipherInit_ex2`, check every return, throw `LLProtectedDataException` on failure, append `decrypted_length` rather than `gcount()` | about 60 to 80 changed |
| `X509_get_notAfter`/`X509_get_notBefore` (`llsechandler_basic.cpp:183-184`) | macros under `OPENSSL_NO_DEPRECATED_1_1_0` | `X509_get0_notAfter`/`X509_get0_notBefore` | 2 |
| `CRYPTO_num_locks`, `CRYPTO_set_locking_callback`, `CRYPTO_THREADID_*` (`llcrashlogger.cpp:771-818`) | no-op macros since 1.1.0, still defined in 3.5 | Delete the locking code (3.x is thread-safe internally) | about -40 |
| `HMAC(EVP_sha1(), ...)` (`exoflickr.cpp:272`) | not deprecated (only `HMAC_CTX_*` are) | none needed; `EVP_Q_mac` if wanted | 0 |
| `RAND_bytes` (`llsechandler_basic.cpp:1431`, `llscripteditorws.cpp:3023`) | unchanged | check the return at `:1431` | 1 |
| `SSL_CTX_set_verify`, `SSL_CTX_set_cert_verify_callback` (`_httpoprequest.cpp:1049-1058`; rust `httprequest.cpp:178-186`) | unchanged | optional: `SSL_OP_LEGACY_SERVER_CONNECT` behind a setting, see section 3 | 0 |
| `X509_verify`, `X509_get_pubkey`, `X509_STORE_CTX_get0_*`, `PEM_*`, `BIO_*`, `ASN1_*`, `OBJ_*`, `BN_bn2hex` (`llsechandler_basic.cpp`) | unchanged | none | 0 |
| `#include <openssl/ossl_typ.h>` | still present (includes `types.h`) | none | 0 |

`OpenSSL.cmake`: static OpenSSL 3 on Windows also needs `ws2_32`,
`advapi32` and `user32` (inferred from OpenSSL's `NOTES-WINDOWS.md`; the
viewer links them already), and on Linux `-ldl -pthread` (also present).

## 3. Behaviour

- **Security level and protocols.** In 3.0, "SSL 3, TLS 1.0, TLS 1.1 and
  DTLS 1.0 no longer [work] at the default security level of 1", SHA-1 is
  dropped from handshake signatures at level 1, and "Secure renegotiation is
  now required by default"
  ([migration guide](https://docs.openssl.org/3.0/man7/migration_guide/)).
  In 3.2 "The default SSL/TLS security level has been changed from 1 to 2"
  (`CHANGES.md`): RSA/DSA/DH keys under 2048 bits are refused.
- **Probe results** (verified, `openssl s_client` 3.5.9, default level 2,
  the viewer's `ca-bundle.crt`, 2026-11-04):

  | Host | Result |
  |---|---|
  | `login.agni.lindenlab.com`, `login.aditi.lindenlab.com` | TLS 1.3, verify OK, RSA 2048 |
  | `secondlife.com`, `asset-cdn.glb.agni.lindenlab.com`, `maps`, `my`, `marketplace`, `search.secondlife.com` | TLS 1.3, verify OK |
  | `id.secondlife.com` | TLS 1.2 ECDHE-RSA-AES128-GCM, secure renegotiation supported |
  | `phoenixviewer.com` (grid list, FS data), `status.secondlifegrid.net` | TLS 1.3, verify OK |
  | `grid.kitely.com`, `hypergrid.org`, `alternatemetaverse.com` (OpenSim) | TLS 1.3, verify OK |
  | `login.osgrid.org:443` | no TLS listener; OSGrid logs in over HTTP |

  Not checked: SL simulator capability hosts on port 12043 (need a live
  session), OpenSim grids beyond this sample (the Firestorm grid list at
  `GridListDownloadURL` holds only the two SL grids), and grids behind
  Mono/.NET TLS stacks. These are covered by the in-world gate.
- **Fallback for legacy servers (optional).** Two levers exist in the
  existing `curlSslCtxCallback`: `SSL_CTX_set_options(ctx,
  SSL_OP_LEGACY_SERVER_CONNECT)` and `SSL_CTX_set_security_level(ctx, 1)`
  (TLS 1.0/1.1 would need level 0). Recommendation: do not lower the
  defaults. If an OpenSim grid reports a failure, add a debug setting,
  off by default.
- **Providers.** Only RC4 (credential store) needs the legacy provider.
  SHA-1 (Flickr HMAC, chain checks), MD5 and AES are in the default
  provider. Nothing uses DES, MD4, Blowfish or RC2 (verified by grep). With
  `no-module`, `providers/build.info` compiles the legacy provider into
  `libcrypto` (`STATIC_LEGACY`); `no-shared` alone does not
  (`Configure` cascades only `pic`/`dso` to `module`). LL's current
  3p-openssl flags (`zlib no-zlib-dynamic threads no-shared`) would
  otherwise produce a `legacy` module that the viewer would have to stage
  and locate. Alchemy, which ships OpenSSL 3 dynamically via vcpkg, does
  exactly that: `OSSL_PROVIDER_set_default_search_path` to the executable
  directory and `OSSL_PROVIDER_try_load(nullptr, "legacy", 1)` (design
  reference only).
- **Credential store.** See risk 1 and the conclusion above. The legacy
  `password.dat` path (`llsechandler_basic.cpp:1869-1892`) uses only
  `LLXORCipher`, not OpenSSL.
- **FIPS.** Not relevant; build without `enable-fips`.
- **Threads and initialisation.** 3.x initialises itself and is
  thread-safe without callbacks (the 1.1.0 model, unchanged); on Windows it
  uses SRWLocks. No init-order change for llcorehttp.
- **Performance.** To be measured: texture and mesh fetch time on a busy
  region and login time, 1.1.1 against 3.5, same build otherwise.

## 4. Packaging and CI

- **Windows:** nothing new to stage while linking statically. The
  `libcrypto-3-x64.dll`/`libssl-3-x64.dll` names only matter for a shared
  build, which is not recommended. Inno installer: no change.
- **Linux:** static as well, so the system `libssl3` is irrelevant at run
  time. CI builds on **ubuntu-26.04** (`.github/workflows/build.yaml:63`),
  not 24.04. It installs `libssl-dev` (`:109`), which nothing in the viewer
  build uses (no `find_package(OpenSSL)` or `pkg_check_modules` for it;
  inferred unused). Keep it out of the include path or drop it so the
  package headers stay the only OpenSSL headers.
- **FreeBSD** (`scripts/ci/freebsd`): image qualification only; it links the
  system curl, which uses base-system OpenSSL 3. No viewer build there yet.
- **macOS:** `darwin64` entries exist in `autobuild.xml` but Vulkanstorm CI
  does not build macOS. Build the package for windows64 and linux64; keep
  `darwin64` pointing at the LL 1.1.1w package or drop it.

## 5. Package availability

| Source | OpenSSL | curl | Usable? |
|---|---|---|---|
| secondlife/3p-openssl | latest `v1.1.1w-r4` (2026-01-14); no 3.x branch or PR | n/a | no |
| secondlife/3p-curl | n/a | latest `v7.54.1-r4` (2026-01-14), built against 1.1.1w | no |
| secondlife/viewer `develop` | pins 1.1.1w-r4 / curl 7.54.1-r4 | | issue [#870](https://github.com/secondlife/viewer/issues/870) open since 2024; [#3112](https://github.com/secondlife/viewer/pull/3112) (3.4.0 test) closed stale |
| FirestormViewer/phoenix-firestorm `master` (2026-11-04) | pins 1.1.1w-r4 / curl 7.54.1-r4 | | no |
| AiraYumi/3p-openssl, 3p-curl (community) | `v3.5.4` (windows64, darwin64, linux64 is a 3.5.5-dev snapshot) | `v8.17.0-r3`; the 7.54.1 build used in #3112 is gone | not for shipping: LL itself declined to build from personal-account packages, and the Linux build is a dev snapshot |

All five Vulkanstorm lines pin the same `openssl`, `curl`, `nghttp2`,
`zlib-ng`, `apr_suite`, `webrtc` and `dullahan` packages (verified by
parsing each line's `autobuild.xml`).

**Building our own:** fork secondlife/3p-openssl and 3p-curl under
anne-skydancer, as already done for `3p-vlc-speakerfill`. For OpenSSL, set
the submodule to `openssl-3.5.x`, add `no-module` (and optionally
`no-engine`, `no-legacy` only if we drop RC4) to the three `Configure`
lines, and keep `zlib no-zlib-dynamic threads no-shared`. For curl, point
its `autobuild.xml` at the new OpenSSL package, rebuild 7.54.1 unchanged,
and patch `lib/vtls/openssl.c` only if the build demands it. Both have
working GitHub Actions autobuild workflows to reuse.

## 6. Licence

- OpenSSL 3.x: "From OpenSSL 3.0 this is replaced by the Apache License v2"
  ([migration guide](https://docs.openssl.org/3.0/man7/migration_guide/)).
  Apache-2.0 is compatible with GPL-3.0 (and with LGPL-2.1 linking), not
  with GPL-2.0-only, which is not offered here. This clears blocker 1 in
  `doc/licensing.md` on `codex/dual-licence` for the binaries.
- Other TLS-related rows in that audit stay as they are: libcurl (MIT-style,
  OK), nghttp2 (MIT, OK), BoringSSL in WebRTC (Apache-2.0, OK), CEF
  (separate process). After the switch, update the OpenSSL row and the
  sentence in `NOTICE.md` that limits the GPL-3.0 option to the source. GLOD
  remains the other blocker until it is removed. The proprietary build
  options (Kakadu, FMOD, Havok, BugSplat) are unrelated to TLS.

## 7. Per-line differences

| Line | OpenSSL-touching code | Note |
|---|---|---|
| vkstorm-release, -devel, -canary, -aleph | identical (`llsechandler_basic.cpp`, `llsecapi.cpp`, `llcrashlogger.cpp`, `exoflickr.cpp`, `llappcorehttp.cpp`, `OpenSSL.cmake`, `CURL.cmake` have the same blobs) | the same patch applies |
| vkstorm-rust | the same, except llcorehttp: `_httpoprequest.*` are replaced by the Rust HTTP service; `indra/llcorehttp/httprequest.cpp:172-186` holds the `SSL_CTX` hook | no extra change; the vkstorm-ffi package needs no rebuild because it uses the viewer's libcurl symbols |
| aleph (tracks Firestorm master) | Firestorm master is still on 1.1.1w | upstream syncs will conflict only in `autobuild.xml` and in the changed hunks |

## 8. Staged plan

Each stage has a gate; stop at a failed gate.

1. **Packages** (feature work, separate repositories).
   `anne-skydancer/3p-openssl` 3.5.x LTS, `no-shared no-module`,
   windows64 + linux64; `anne-skydancer/3p-curl` 7.54.1 against it.
   *Gate:* both packages build in CI; `openssl list -providers -provider
   legacy` from the package build shows the built-in legacy provider; the
   curl test suite's TLS tests pass.
2. **Viewer change** on `codex/openssl3` from `vkstorm-devel`:
   `autobuild.xml`, `OpenSSL.cmake` if needed, `llsecapi.cpp` (drop
   ENGINE/init), `llsechandler_basic.cpp` (RC4 via private legacy context,
   checked returns, `get0_` accessors), `llcrashlogger.cpp` (drop locking),
   remove the dead `wrapper.sh` preload.
   *Gate:* RelWithDebInfo builds with `/WX` and `-Werror` on Windows and
   Linux and the staged viewer runs.
3. **Credential-store compatibility.**
   *Gate:* (a) `llsechandler_basic_test` test 3 passes; (b) a new test
   reads a `bin_conf.dat` fixture written by the current 1.1.1 viewer and
   writes it back, and the 1.1.1 viewer reads the result (round trip in
   both directions, so a downgrade keeps credentials); (c) an injected
   cipher-fetch failure throws instead of writing garbage; (d) manual: a
   profile with saved SL and OpenSim logins, including MFA, logs in after
   the upgrade without re-entering passwords.
4. **TLS against servers.**
   *Gate:* SL login on agni and aditi, region crossings and teleports
   (simulator capabilities on 12043), inventory and asset fetch, marketplace
   and profile web, grid list fetch; OpenSim login on OSGrid, Kitely and
   at least two HTTPS grids; texture and mesh fetch times within noise of
   the 1.1.1 build on the same region.
5. **CI on both platforms.** *Gate:* `build.yaml` green for Windows and
   Linux on the PR, and the installer payload check passes on a Release run.
6. **Per-line integration.** One integration PR each to vkstorm-release
   (Release + LTO + installer), vkstorm-canary, vkstorm-rust and
   vkstorm-aleph, cherry-picked from the devel PR. Update
   `doc/licensing.md` and `NOTICE.md` on each line once the binary links
   no GPL-incompatible OpenSSL. *Gate:* each line's CI, then the owner's
   in-world check after release.
7. **Later, separate:** decide whether to move the credential store to a
   modern AEAD format with an RC4 read fallback (which would drop the legacy
   provider), and whether to move to curl 8.x with HTTP/2 (its own study:
   pipelining, error-code merge, Rust `vkstorm-http` parity).

## Effort

| Stage | Estimate |
|---|---|
| 1. OpenSSL package | 1 to 2 days |
| 1. curl rebuild (more if 7.54.1 needs source patches) | 1 to 3 days |
| 2. Viewer change | 1 day |
| 3. Credential tests and fixtures | 1 day |
| 4. Server tests and timing | 1 to 2 days |
| 5. CI | 0.5 day |
| 6. Four line integrations | 2 days |
| **Total** | **8 to 12 working days** |

## What was inspected

- Source: `git grep` for `#include <openssl/` and the listed API prefixes on
  all five lines; full reads of the protected-store code, the init code,
  the curl SSL callbacks and the crash logger's locking code.
- Build: `vkr/inv-r2/build-vc180-64` (RelWithDebInfo, devel-based):
  `dumpbin /dependents` on `vulkanstorm-bin.exe` and `llwebrtc.dll`,
  `dumpbin /exports` on `llwebrtc.dll`, and a scan of `packages/lib/release`
  for OpenSSL API names. No Linux build was inspected (`ldd`), so the Linux
  rows rely on the CMake flags.
- OpenSSL 3.5.4 sources via GitHub: `Configure` (disable cascades),
  `providers/build.info`, `ssl/ssl_cert.c`, `ssl/statem/statem_clnt.c`,
  `include/openssl/{hmac,engine,crypto,x509,err,evp}.h`, `CHANGES.md`,
  `doc/man7/ossl-guide-migration.pod`.
- Run: OpenSSL 3.5.9 CLI (Git for Windows): RC4 with and without the legacy
  provider, the RC4 decryption of the test 3 store blob, and the
  `s_client` probes above.
- Upstream: secondlife/3p-openssl and 3p-curl releases, branches and PRs;
  secondlife/viewer `develop` `autobuild.xml`, issue #870, PR #3112,
  PR #4892; Firestorm `master` `autobuild.xml`; Alchemy `develop`
  `llsecapi.cpp`, `llsechandler_basic.cpp` and `vcpkg.json`; the
  vkstorm-core-rust `Cargo.lock`, `vkstorm-http` and `vkstorm-audio` link
  setup.
