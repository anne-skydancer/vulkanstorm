# OpenSSL 3.5 and curl 8: prototype patch set

Status: prototype, 2026-11-05. Draft PR only; nothing here goes to a base
line. Branch `codex/openssl35-curl8-prototype` from `origin/vkstorm-devel`
at `a5290e8c81`; Rust core branch `codex/curl8-prototype` from
vkstorm-core-rust `main` at `08a5cbd`. Follows the two studies,
`doc/openssl3-impact.md` (PR #314) and `doc/curl8-feasibility.md` (PR #319).

**Follow-up, 2026-11-05 afternoon** (gaps in the first pass closed; see
[Follow-up](#follow-up-2026-11-05-gaps-closed)): the credential-store hotfix
`68ec9057a3` landed on every line after this branch was cut and supersedes
a3, a4 and a7, so it is cherry-picked here (`528cba6c39`) and step (a) is
now a1, a2, a5, a6 only; the certificate-failure path at login (b4, b5) was
run against local servers with bad certificates, on this build and on the
installed release viewer; the smoke test was repeated at b10; the per-line
check was redone on the current line tips. The worktrees moved: `vkr\` and
`repos\work\` are now `vulkanstorm\work\` (this worktree
`work\tls-prototype`, the packages `work\tls-packages`, the Rust worktree
`vkstorm-core-rust\work\curl8-prototype`).

Every item is marked **verified** (built, run or inspected here) or
**inferred** (reasoned, not run). Windows x64 only was built and run; Linux
is CI's and nothing Linux-specific was run locally (no WSL on this machine).

## Answers

- **curl 7.54.1 builds and works on OpenSSL 3.5. Verified.** LL's 3p-curl
  source and build script, unchanged, built against the OpenSSL 3.5.9
  package in 2 minutes with no source patch: three `C4090` const warnings
  (`EVP_PKEY_get0_RSA/DSA/DH` return `const` in 3.x, `lib/vtls/openssl.c`
  2623, 2653, 2685) and nothing else. The curl tool negotiated TLS 1.3 with
  `login.agni.lindenlab.com` and HTTP/2 with `secondlife.com`, verifying
  against the viewer's `ca-bundle.crt`. The viewer linked it, passed its
  1278 unit and integration tests (6 skipped known failures, none failed),
  reached the login screen, and fetched the HTTPS grid list through
  llcorehttp with peer and host verification on. One cosmetic defect: 7.54.1
  decodes the 3.x version number with the 1.x scheme and reports
  `OpenSSL/3.5.0i` (in `curl_version()` and so in the viewer log and About
  box). The studies' main open risk is closed: **the OpenSSL step can ship
  with curl 7.54.1** (rebuilt), pipelining and all.
- **Step (a), OpenSSL 3.5 with curl 7.54.1** needs one build fix
  (`llsecapi.cpp`, ENGINE) and one behavioural fix that the compiler does
  not report (the credential store), plus cleanups. The credential store is
  now on every line already: the hotfix `68ec9057a3` (release; devel
  `d91f465450`, canary `103718b763`, rust `26da4e02e5`, aleph `0a6cff7455`)
  carries a3, a4 and a7 with the OpenSSL 3 code under
  `OPENSSL_VERSION_NUMBER >= 0x30000000L`. What step (a) still adds is four
  commits, a1, a2, a5, a6: about +32 −101 lines, plus the package.
- **Step (b), curl 8.23.0** needs one link fix on Windows (`iphlpapi`), two
  Linux `-Werror` fixes, the certificate-failure fix at login, and the
  pipelining replacement in llcorehttp and in the Rust `vkstorm-http`.
  Ten commits, +285 −274 lines in 16 files (b6 removes more than it adds;
  b8–b10 are review follow-ups).
  The viewer builds green on curl 8.23.0, its curl-linked tests pass (the
  restored llcorehttp suite after two expectation fixes), and it reaches
  the login screen with the HTTPS grid list fetched. What curl 8 does to
  texture and mesh load times is not measured here (the study's gate G3).
- Effort, updated from what was hit: see [Effort](#effort).

**Per line** (rechecked on the current tips in the follow-up): every line
now has the credential-store hotfix, so a3, a4 and a7 drop out;
vkstorm-release takes the other 14 code commits as-is. On canary
and aleph the net change applies to every file but one: the llcorehttp
test registration in `CMakeLists.txt` must be placed by hand (commit by
commit, the test hunks after it follow). On the rust line the step (a)
code applies as-is (the line registers the llcorehttp test already); its
C++ llcorehttp internals are gone, so b2 is moot there, b6 and b8–b10
are done in Rust instead (the core branch), and its parity fixture must
be re-recorded. See [Per-line notes](#per-line-notes).

## Packages

All three were built with autobuild 3.10.2, Visual Studio 2026 (MSVC
19.51.36260, toolset 14.51.36231), `AUTOBUILD_VSVER=180` and
`FirestormViewer/fs-build-variables` (the viewer CI's), as packages that
could be published later: each repository follows `3p-vlc-speakerfill`
(source downloaded and hash-pinned, `VERSION.txt` as
`<upstream version>.<package revision>`, `canonical_repo`
anne-skydancer/..., a windows64 + linux64 workflow with a tag-driven
release job). Nothing was pushed or published; the forks are local clones
of secondlife/3p-openssl and secondlife/3p-curl with their push URL
disabled, under `C:\Users\annes\source\repos\work\`.

### OpenSSL 3.5.9: `work\3p-openssl`, branch `vks/openssl-3.5.9` (local commit `cd51564`)

| | |
|---|---|
| Source | `openssl-3.5.9.tar.gz` from the GitHub release linked by openssl-library.org; SHA-256 `603f5602e2eef00d77fbd429d34dcd5822bb301757a1bc9cdb24c670f1eb859a`, pinned in `build-cmd.sh`; GPG signature checked in the OpenSSL study (subkey `C46E D3F2 CBEF DA1F DAAD A442 64ED 7B1D CCE7 1CB2`) |
| Configure (Windows) | `perl Configure VC-WIN64A zlib no-zlib-dynamic threads no-shared no-module -DUNICODE -D_UNICODE -FS --with-zlib-include=<zlib-ng> --with-zlib-lib=<zlib.lib>`, `CFLAGS=/nologo /W3 /wd4090` + `LL_BUILD_RELEASE` with `/Z7` (`/MD /O2 /Ob2`; `/GR` and `/std` removed), Strawberry Perl, NASM 3.02, `nmake` (`jom` if present) |
| Configure (Linux, darwin) | LL's lines plus `no-module`; written, **not run** |
| Changes vs LL's script | tarball instead of the git submodule; `no-module` (legacy provider compiled into `libcrypto`, `-DSTATIC_LEGACY`); OpenSSL's own `/nologo /W3 /wd4090` kept in front of `LL_BUILD_RELEASE` (a `CFLAGS` variable replaces them otherwise); a smoke test; `LICENSE.txt` (3.x name); Apache-2.0 metadata; version from `VERSION.txt` (`3.5.9.1`) instead of `use_scm_version` |
| Smoke test | `openssl list -providers` shows `default` and `legacy` 3.5.9 active; RC4 through the legacy provider gives the known answer `9fe92c92268c6ab869a9fa` (key `00..0f`, `vulkanstorm`; checked against a textbook RC4) — **verified** |
| Test suite | `nmake test` with `HARNESS_JOBS=8`: 347 files, 3822 tests, **PASS** (verified) |
| Time | 21 min 48 s for build and test suite (nmake, 8 logical CPUs) |
| Archive | `openssl-3.5.9.1-windows64-1.tar.zst`, 10,352,986 bytes, SHA-1 `96e36bea4f5b126d064e95e0dfdab4a912088150`, SHA-256 `c44e484e1bc6e2cb703413c5a7de813b703c74ae899f13847b2d89fe068b3cca`; 146 entries: `include/openssl/*.h`, `lib/release/libcrypto.lib` (62 MB with `/Z7` debug info), `lib/release/libssl.lib`, `LICENSES/openssl.txt` |

Compiled-in paths stay OpenSSL's Windows defaults (`OPENSSLDIR`
`C:\Program Files\Common Files\SSL`, engines and modules under
`C:\Program Files\OpenSSL\lib`), all admin-only, as LL's 1.1.1 build. On
Linux LL's `--openssldir=share` bakes the build machine's stage path, also
as before (inferred).

### curl 7.54.1 against OpenSSL 3.5: `work\3p-curl`, branch `vks/7.54.1-openssl35` (`42c866a1`)

| | |
|---|---|
| Source | LL's vendored 7.54.1 tree and `build-cmd.sh`, unchanged |
| Change | `autobuild.xml`: the `openssl` dependency points at the 3.5.9.1 archive (a `file:///` URL until it is published; linux64 still names LL's 1.1.1w) |
| Build | 2 min 9 s; 3 × `C4090` in `lib/vtls/openssl.c`; no error. LL's flags have no `/W3`, so 3.0 deprecation warnings (`C4996`, level 3) are not shown; GCC would print them as warnings (no `-Werror` in curl's build), inferred |
| `curl --version` | `curl 7.54.1 (Windows) libcurl/7.54.1 OpenSSL/3.5.0i zlib/1.3.1.zlib-ng nghttp2/1.64.0` |
| Fetches (curl tool, viewer `ca-bundle.crt`) | `login.agni.lindenlab.com` TLS 1.3 `TLS_AES_256_GCM_SHA384`, verify OK, ALPN http/1.1; `secondlife.com`, `id`, `map` over HTTP/2; `http://asset-cdn.glb.agni.lindenlab.com/?texture_id=89556747-…` 200 — verified |
| Archive | `curl-7.54.1-1-windows64-1.tar.zst`, 1,151,601 bytes, SHA-1 `f0b81174297e51927a8b5dbb5b191c1ffbdf3b49`, SHA-256 `076c9f9411a21cf69476c75fc0bd5262f3057e2656ded9566e5328b091e0f27a` |

### curl 8.23.0: `work\3p-curl`, branch `vks/curl-8.23.0` (`eaf9458c`)

| | |
|---|---|
| Source | `curl-8.23.0.tar.gz` from curl.se, SHA-256 `e3de77cfa6367083c776a26647fc9f633b538c06c3baa73d931a252ac8f72b49` (matches `https://curl.se/info`), pinned in `build-cmd.sh`; `gpg --verify` of `curl-8.23.0.tar.gz.asc` against `https://daniel.haxx.se/mykey.asc` in a throwaway keyring: "Good signature from Daniel Stenberg", key `27ED EAF2 2F3A BCEB 50DB 9A12 5CC9 08FD B71E 12C2` — verified. 8.23.0 (2026-10-14) is current; the next release is due 2026-12-09 |
| CMake options | static only (`BUILD_SHARED_LIBS=OFF`, `BUILD_STATIC_LIBS=ON`, curl tool linked statically); `CURL_USE_OPENSSL=ON`, `OPENSSL_USE_STATIC_LIBS=ON`; `USE_NGHTTP2=ON` (`-DNGHTTP2_STATICLIB`); `CURL_ZLIB=ON` with zlib-ng; `ENABLE_THREADED_RESOLVER=ON`, `ENABLE_ARES=OFF`; off: `USE_LIBIDN2`, `CURL_USE_LIBPSL`, `CURL_BROTLI`, `CURL_ZSTD`, `CURL_USE_LIBSSH2`, `CURL_USE_LIBSSH`, `CURL_USE_GSSAPI`, `CURL_USE_GSASL`, `USE_NGTCP2`, `USE_QUICHE`, `CURL_ENABLE_NTLM`; `CURL_DISABLE_` `LDAP`, `LDAPS`, `DICT`, `FILE`, `FTP`, `GOPHER`, `IMAP`, `IPFS`, `MQTT`, `POP3`, `RTSP`, `SMTP`, `TELNET`, `TFTP`, `WEBSOCKETS`; `BUILD_TESTING`, examples and docs off; `CURL_USE_PKGCONFIG=OFF`, `CURL_USE_CMAKECONFIG=OFF` (nothing from the build machine); Windows: `ENABLE_UNICODE=ON` (LL's flags define `UNICODE`), `/MD`; `CURL_LTO=OFF` (as LL's packages: `/GL` objects would tie the package to one compiler version) |
| Dependencies given explicitly | `OPENSSL_INCLUDE_DIR`, and on MSVC `LIB_EAY_RELEASE`/`SSL_EAY_RELEASE` (CMake's FindOpenSSL derives `OPENSSL_*_LIBRARY` from them with MSVC; the first attempt, with `OPENSSL_CRYPTO_LIBRARY` only, failed with "Could NOT find OpenSSL (missing: OPENSSL_CRYPTO_LIBRARY)"), `ZLIB_*`, `NGHTTP2_*` |
| Smoke test (in `build-cmd.sh`) | version line names `curl 8.23.0`, `libcurl/8.23.0`, `OpenSSL/3.5.`, `zlib/`, `nghttp2/`; protocols exactly `http https`; features include AsynchDNS HTTP2 HTTPS-proxy libz SSL threadsafe and none of IDN PSL brotli zstd NTLM Kerberos SPNEGO GSS-API HTTP3 — verified |
| `curl --version` | `curl 8.23.0 (Windows) libcurl/8.23.0 OpenSSL/3.5.9 zlib/1.3.1.zlib-ng nghttp2/1.64.0`; Protocols: `http https`; Features: `alt-svc AsynchDNS HSTS HTTP2 HTTPS-proxy IPv6 Largefile libz SSL threadsafe Unicode UnixSockets` |
| Build | 1 min 32 s, 0 warnings at curl's picky level (`-W4 -Wall` minus curl's exclusions) |
| Fetches (curl tool) | `login.agni.lindenlab.com`: TLS 1.3, `TLS_AES_256_GCM_SHA384 / X25519MLKEM768 / RSASSA-PSS` (OpenSSL 3.5 offers the post-quantum hybrid group by default and the login server accepts it), verify OK; `secondlife.com`, `map` HTTP/2; CDN texture over `http:` 200; `ftp://` and `file://` refused ("Protocol ... is disabled") — verified |
| Archive | `curl-8.23.0.1-windows64-1.tar.zst`, 1,826,028 bytes, SHA-1 `08ce6d42adc7adc35b226f0745cad03ef244ac76`, SHA-256 `4ff36767b70ba81b05720a88c554ab126b62c81afcc900522ea2deb98fbb5613` |
| Link dependencies (from its `CURLTargets.cmake`) | `bcrypt advapi32 crypt32 ws2_32 iphlpapi`, plus OpenSSL, zlib, nghttp2 |

Not done: curl's own test suite (the study's gate G0 asks for its HTTP and
TLS tests). The OpenSSL test suite did run.

### How the worktree consumed them

Only through autobuild, and only in the worktree:

```
cd work\tls-prototype                         (was vkr\tls-prototype)
copy autobuild.xml my_autobuild.xml            (my_autobuild.xml is in .gitignore)
set AUTOBUILD_CONFIG_FILE=my_autobuild.xml
autobuild installables edit openssl platform=windows64 hash=96e36bea4f5b126d064e95e0dfdab4a912088150 hash_algorithm=sha1 url=file:///C:/Users/annes/source/repos/vulkanstorm/work/tls-packages/3p-openssl/openssl-3.5.9.1-windows64-1.tar.zst
autobuild installables edit curl platform=windows64 hash=f0b81174297e51927a8b5dbb5b191c1ffbdf3b49 hash_algorithm=sha1 url=file:///C:/Users/annes/source/repos/vulkanstorm/work/tls-packages/3p-curl/curl-7.54.1-1-windows64-1.tar.zst
autobuild configure -A 64 -c RelWithDebInfoFS_open -- --no-opensim --lto "-DLL_TESTS:BOOL=TRUE"
autobuild build -A 64 -c RelWithDebInfoFS_open --no-configure
```

For step (b) the `curl` entry was edited to
`curl-8.23.0.1-windows64-1.tar.zst` (SHA-1 above). `autobuild configure`
does not reinstall a package when only `my_autobuild.xml` changes (the
CMake prebuilt check watches `autobuild.xml`), so the switch also needs
`autobuild install --install-dir=build-vc180-64\packages curl` with the same
`AUTOBUILD_CONFIG_FILE`; autobuild then removes 7.54.1's files (its
`curlbuild.h`, `curlrules.h`) and installs 8.23.0. `installed-packages.xml`
records the `file:///` URLs. The committed `autobuild.xml` keeps LL's
packages, so CI builds this branch against OpenSSL 1.1.1 and curl 7.54.1
and fails at the OpenSSL 3 calls (expected; see the PR).

After the move of the worktrees the two `file:///` URLs in `my_autobuild.xml`
were pointed at `work\tls-packages` (same SHA-1s, rechecked), and the build
directory had to be configured afresh: its `CMakeCache.txt` named the old
source directory (kept as `CMakeCache.pre-move.txt`). `autobuild configure`
then found OpenSSL 3.5.9.1 and curl 8.23.0.1 already installed.

## Step (a): OpenSSL 3.5 with curl 7.54.1

### What the switch breaks (verified)

The unmodified sources (base `a5290e8c81`) were compiled against the 3.5.9
package headers in the projects that build them outside newview's
precompiled header (`INTEGRATION_TEST_llsecapi`,
`INTEGRATION_TEST_llsechandler_basic`, `llcrashlogger`), with the viewer's
flags (`/W3 /WX`):

```
llsecapi.cpp(49,5): error C2220: the following warning is treated as an error
llsecapi.cpp(49,5): warning C4996: 'ENGINE_load_builtin_engines': Since OpenSSL 3.0
llsecapi.cpp(51,25): warning C4996: 'ENGINE_by_id': Since OpenSSL 3.0
llsecapi.cpp(53,9): warning C4996: 'ENGINE_set_default': Since OpenSSL 3.0
```

That is the only build error. `llsechandler_basic.cpp` compiles without a
warning: `EVP_rc4()` still exists and `X509_get_notAfter/notBefore` are
deprecated macro aliases, so **the credential-store failure is silent at
build time** and appears only at run time (the credstore harness's control
test: today's code on 3.5.9 cuts a 3,222-byte store to its 16-byte salt).
`llcrashlogger.cpp` compiles (its OpenSSL 1.0 locking calls are no-op
macros). Nothing fails to link: `OpenSSL.cmake` needs no change on Windows
(static `libssl`/`libcrypto` plus `Crypt32`; `bcrypt`, which OpenSSL 3's
seeding uses, comes in through `#pragma comment(lib, "bcrypt.lib")` in
`providers/implementations/rands/seeding/rand_win.c`).
`dumpbin /dependents vulkanstorm-bin.exe` shows no OpenSSL DLL; the binary
holds `OpenSSL 3.5.9 29 Sep 2026` and `libcurl/7.54.1`.

### Changes

| # | Commit | Files | Why | Size | Status |
|---|---|---|---|---|---|
| a1 | `328debadba` | `indra/newview/llsecapi.cpp` | ENGINE/rdrand init deprecated (the build error above) and removed in OpenSSL 4; `ERR_load_crypto_strings`/`OpenSSL_add_all_algorithms` automatic since 1.1.0. The 3.x DRBG seeds from the OS generator, which mixes in RDRAND/RDSEED | +7 −24 | verified (builds; viewer runs) |
| a2 | `9617af6ef5` | `llsechandler_basic.cpp`, `.h` | `X509_get0_notAfter/notBefore` (const) instead of the deprecated aliases; `cert_date_from_asn1_time(const ASN1_TIME*)`. Not an error at the default API level; it would be with `OPENSSL_NO_DEPRECATED_1_1_0`. Also valid on 1.1.1 | +6 −4 | verified (the certificate tests, which check validity dates, pass) |
| a3 | `66d5e3a5d4` | `llsechandler_basic.cpp` | Credential store: the credstore harness's tested RC4 code (private `OSSL_LIB_CTX`, `OSSL_PROVIDER_load(ctx, "legacy")`, `EVP_CIPHER_fetch(ctx, "RC4")`, key length, every `EVP_CipherInit_ex2`/`Update`/`Final_ex` return and each chunk's output length checked). Read: whole file, one checked pass, `LLProtectedDataException` on failure. Write: whole file built in memory; `RAND_bytes` checked; the temporary file must be written and closed without error; then `LLFile::rename` (`MoveFileExW(MOVEFILE_REPLACE_EXISTING)` on Windows, `rename()` elsewhere) replaces the store, which is never removed first. Closes both loss routes from the study (failed cipher; exception in the encryption block falling through to the remove-then-rename) and a third one found here: a failed write of the temporary file (disk full, locked file) was not detected and also ended in remove-then-rename | +179 −105 | verified (tests 3, 10, 11; credstore harness). **Superseded** by the hotfix `68ec9057a3`, on every line; cherry-picked here as `528cba6c39` |
| a4 | `974b20076b` | `indra/newview/tests/llsechandler_basic_test.cpp` | Test 10: a store written by the handler decrypts with a textbook RC4 (no OpenSSL) to the stored LLSD, so the on-disk format is the one 1.1.1 viewers read. Test 11: a directory in the place of `<store>.tmp` makes the write fail; the store must stay byte-identical and readable, and a later write must replace it. Test 3 (existing) reads the 1.1.1-era RC4 fixture | +112 −1 | verified (11/11 pass; test 11 logs "Could not write the protected data store), existing store kept"). **Superseded** by the hotfix (its tests 10–12) |
| a5 | `a6d5ea9a57` | `indra/llcrashlogger/llcrashlogger.cpp`, `.h` | `CRYPTO_num_locks`/`CRYPTO_set_locking_callback`/`CRYPTO_THREADID` have been no-op macros since 1.1.0; removed with their mutex list | +3 −66 | verified (compiles; Windows builds the library but only the Linux crash logger links it) |
| a6 | `819a54439d` | `indra/llcorehttp/CMakeLists.txt`, `tests/test_httprequest.hpp` | `INTEGRATION_TEST_llcorehttp` registered again (Firestorm had it commented out): the gate for an OpenSSL or curl change. Its BUG-2295 test fails under Firestorm's pipelining desync check (the peer answers a 0-49 range with 0-75 and the check turns it into a 503), so it is skipped with that reason until step (b) removes the check | +16 −7 | verified (63 tests: 59 pass, 4 skipped, 0 failed) |
| a7 | `67123785bc` | `llsechandler_basic.cpp`, `tests/llsechandler_basic_test.cpp` | Review follow-up (#322): the store is read through `istream::read` in chunks, not an `istreambuf_iterator`, which lets libstdc++'s `basic_filebuf::underflow` throw `std::ios_base::failure` (a directory, an I/O error) past the `LLProtectedDataException` handler; a read error now raises `LLProtectedDataException("Config file cannot be read.")`. Test 12: a directory as the store path lets nothing but `LLProtectedDataException` out of `init()` | +41 −4 | verified on Windows (12/12; there a directory does not open as a file); the Linux path inferred. **Superseded** by the hotfix |

Total step (a) as first built: +364 −211 in 8 files (a7 is the review follow-up). With the hotfix on every line, what remains is a1, a2, a5, a6.

Not changed: `exoflickr.cpp` (one-shot `HMAC`, not deprecated),
`llscripteditorws.cpp` (already checks `RAND_bytes`), `OpenSSL.cmake` (no
change needed on Windows; Linux already links `-ldl -pthread`, inferred),
`linux_tools/wrapper.sh` (its `libcrypto.so.1.0.0` preload is dead code;
left alone, Linux not run).

### Results (verified)

- **Build:** RelWithDebInfo, LTO, `LL_TESTS=TRUE`: 0 errors, staged in
  `build-vc180-64\newview\RelWithDebInfo`. Wall clock 3 h 44 min, of which
  the machine slept 3 h 17 min (Kernel-Power events 42/107 at 03:06 and
  06:23): about 27 minutes of building and testing. The later builds ran
  with a keep-awake request.
- **Unit and integration tests run by the build:** 92 test executables,
  1278 tests: 1272 passed, 6 skipped known failures (`llmainthreadtask`,
  `llsys`, `m3math`, `mathmisc`, `v3dmath`, `llhost`, each skipped before
  this change too), 0 failed. `INTEGRATION_TEST_llsechandler_basic`: 11/11.
- **`INTEGRATION_TEST_llcorehttp`** (restored, against the local Python
  peer): 63 tests, 59 passed, 3 skipped known failures, BUG-2295 failed for
  the reason above, then skipped.
- **Credstore harness** (`scripts/tests/openssl3_credstore`), rerun against
  the 3.5.9.1 *package* (`CREDSTORE_OPENSSL35` pointed at its headers and
  libraries) and the viewer's 1.1.1w package: 7 passed (fixture, byte-identical
  writes, cross-reads with fixed and random salts, clean failures without
  the legacy provider, the control showing today's code losing the store),
  1 skipped: the real-store check is opt-in (`CREDSTORE_REAL=1`) and reads
  the owner's own `bin_conf.dat`, so it was not rerun without asking; it
  passed in the study on 2026-11-04.
- **Smoke test** (staged viewer, `APPDATA` and `LOCALAPPDATA` pointed at an
  empty scratch profile so the real profile and its `bin_conf.dat` were never
  opened; SHA-256 and timestamp of the real store compared before and after:
  unchanged): login screen (`STATE_LOGIN_WAIT`) after 22 s. The log shows
  `libcurl version is: libcurl/7.54.1 OpenSSL/3.5.0i zlib/1.3.1.zlib-ng
  nghttp2/1.64.0`, `ca-bundle.crt` loaded (145 certificates; 3 expired
  roots rejected by the viewer's own date check), and
  `GridListRequest: got new list` from
  `https://phoenixviewer.com/app/fsdata/grids.xml`, which llstartup fetches
  through llcorehttp with peer and host verification on, so through curl
  7.54.1's OpenSSL 3.5 handshake and the viewer's own `sslVerify` chain check.
  No TLS, certificate or curl error. The FS data files come over plain
  `http://phoenixviewer.com` (one 403 for `defaults.1.0.1.xml`, a version the
  server does not know). The login page itself is CEF (its own BoringSSL),
  not this TLS stack. Not logged in. The viewer ignored `WM_CLOSE` at the
  login screen for 60 s and was then terminated.
- **CDN fetch through llcorehttp:** `http_texture_load -w -p 5 -c 8 -u
  "http://asset-cdn.glb.agni.lindenlab.com/?texture_id=%s"` on 8 default
  texture UUIDs from `settings.xml`: 8 successes, 238,211 bytes, 0 HTTP or
  API errors, 0.22 s (pipelined, depth 5).
- `scripts/tests/check_release_hooks.py`: passed.

## Step (b): curl 8.23.0

### What the switch breaks (verified)

The step (a) tree built against the curl 8.23.0 package:

- **Windows compile:** nothing. curl 8 marks deprecated API only for GCC
  12.1+ and clang (`CURL_DEPRECATED` is empty for MSVC).
- **Windows link:** 11 executables, including `vulkanstorm-bin.exe`,
  `http_texture_load.exe` and nine test programs:
  `libcurl.lib(peer.obj) : error LNK2001: unresolved external symbol __imp_if_nametoindex`
  (`iphlpapi`, which a static curl 8 needs).
- **Linux `-Werror`** (not built here; reproduced on Windows with clang and
  `-Werror` on the same constructs against the 8.23.0 headers, which apply
  the same attribute for GCC): seven errors, none with the 7.54.1 headers:
  ```
  'CURLINFO_SIZE_DOWNLOAD' is deprecated: since 7.55.0. Use CURLINFO_SIZE_DOWNLOAD_T
  'CURLINFO_SPEED_DOWNLOAD' is deprecated: since 7.55.0. Use CURLINFO_SPEED_DOWNLOAD_T
  'CURLFORM_COPYNAME' is deprecated: since 7.56.0. Use curl_mime_name()
  'CURLFORM_FILE' is deprecated: since 7.56.0. Use curl_mime_filedata()
  'curl_formadd' is deprecated: since 7.56.0. Use curl_mime_init()
  'CURLOPT_HTTPPOST' is deprecated: since 7.56.0. Use CURLOPT_MIMEPOST
  'curl_formfree' is deprecated: since 7.56.0. Use curl_mime_free()
  ```
  The replacement code compiles cleanly under the same check
  (`clang++ -Werror -Wall -Wextra`).
- **Behaviour, no build error:** pipelining is ignored silently;
  `CURLE_SSL_CACERT` and `CURLE_SSL_PEER_CERTIFICATE` are both 60; the
  login's SSL message mapping loses its case; libcurl's error texts change
  ("Couldn't connect to server" is now "Could not connect to server"; no
  viewer code matches on them, only logs and one Rust test did).

### Changes

| # | Commit | Files | Why | Size | Status |
|---|---|---|---|---|---|
| b1 | `a1a46dcc86` | `indra/cmake/CURL.cmake` | Static curl 8's Windows libraries (`bcrypt advapi32 crypt32 ws2_32 iphlpapi`, from its `CURLTargets.cmake`): the link error above | +6 | verified (links) |
| b2 | `e3b6166fd5` | `indra/llcorehttp/_httpoprequest.cpp` | `CURLINFO_SIZE_DOWNLOAD_T`/`SPEED_DOWNLOAD_T` (`curl_off_t`, converted to the `F64` stats); old calls kept under `LIBCURL_VERSION_NUM < 7.55` | +12 | verified on Windows; Linux error and fix checked with clang only |
| b3 | `ac93b575e0` | `indra/linux_crash_logger/linux_crash_logger.cpp` | The mime API Firestorm had drafted and commented out (`curl_mime_*`, `CURLOPT_MIMEPOST`) for curl 7.56+; the form API stays for older curl. Drops a stray handle that was never cleaned up | +16 −17 | inferred (Linux-only file; checked with clang `-Werror` against the 8.23.0 headers, not built on Linux) |
| b4 | `c2434700d9` | `llsecapi.h`, `llappcorehttp.cpp`, `llxmlrpclistener.cpp` | `sslVerify` records its verdict in the certificate data (`CERT_VALIDATION_FAILURE`: `untrusted_ca` or `invalid`); the XML-RPC listener names the failure from it (`SSL_CACERT` only for an untrusted CA, else `SSL_PEER_CERTIFICATE`) and attaches the certificate, independent of curl's code. `llstartup.cpp:1944-1961` then offers "trust this CA" only for an unknown CA. curl's own verification failures carry no verdict and keep the generic error | +26 −10 | **verified** to the dialog choice: the certificate probe (follow-up) gets `SSL_CACERT` + `untrusted_ca` for an unknown CA and `SSL_PEER_CERTIFICATE` + `invalid` + the certificate for an expired one; the dialog itself is masked by finding 3 |
| b5 | `f4a87d0c1f` | `llxmlrpctransaction.cpp` | `case CURLE_PEER_FAILED_VERIFICATION:` → `ssl_connect_error` for curl 7.81+, where the `CURLE_SSL_CACERT` case is compiled out; otherwise a certificate failure shows "server is down" | +7 | **verified** by the certificate probe: every certificate failure, the hostname mismatch included, now gives the `ssl_connect_error` text; release gives the generic text for the expired and wrong-host cases |
| b6 | `92a4f39218` | `_httppolicyclass.h`, `_httppolicy.cpp/.h`, `_httplibcurl.cpp/.h`, `_httpoprequest.cpp/.h`, `httprequest.h`, `llappcorehttp.cpp`, `tests/test_httprequest.hpp` | Pipelining replacement, below. BUG-2295 test no longer skipped | +90 −235 | verified: builds; llcorehttp suite incl. BUG-2295 passes; viewer and `http_texture_load` run. Load-time effect not measured (G3) |
| b7 | `1c79e63d1a` | `indra/llcorehttp/tests/test_httprequest.hpp` | With curl 8 the restored suite's "Header generation for HttpRequest GET" and "... PUT" failed: they require `Accept-Encoding`, which llcorehttp sends only with curl before 7.81 (the guard below). For 7.81+ they now require it absent; POST is unchanged | +33 | verified (63 tests: 60 pass, 3 skipped, 0 failed) |
| b8 | `3500ce3c11` | `_httplibcurl.cpp`, `tests/test_httprequest.hpp` | Review follow-up (#322): b6 had also dropped the desync check's offset test. A 206 whose Content-Range starts elsewhere than asked is refused again, with the retryable `HE_INV_CONTENT_RANGE_HDR` and nothing switched off: `lltexturefetch.cpp:1844` skips realignment when the reply offset is 0 and would append such a reply to its data. A longer reply at the asked offset is still delivered (BUG-2295). New block in the BUG-2295 test: `bytes 0-75` for a request at offset 10 must fail so | +47 −1 | verified (63 tests: 60 pass, 3 skipped; the new block logs the refusal and passes) |
| b9 | `523bdfdb40` | `_httplibcurl.cpp`, `tests/test_httprequest.hpp`, `tests/test_llcorehttp_peer.py` | Second review follow-up (the same check in Rust, core #26): b8 skipped a 206 whose Content-Range was missing or not parsed (`mReplyOffset` and `mReplyLength` stay 0), so such a reply to a request at a later offset was delivered unchecked. The length guard goes: such a reply reads as offset 0 and is refused for a request at a later offset, as Firestorm's check did; at offset 0 it passes. The test peer gains `/bug2295/no_range/` (a 206 without Content-Range) and the BUG-2295 test a fifth block: that reply to a request at offset 10 must fail with `HE_INV_CONTENT_RANGE_HDR` | +41 −5 | verified (63 tests: 60 pass, 3 skipped; both refusals logged) |
| b10 | `42c7ab06f6` | `_httplibcurl.cpp` | Comment only: the texture fetcher (`lltexturefetch.cpp:2474`) and the mesh repository (`llmeshrepository.cpp:3710`) take a 206 without a useful Content-Range as the asked range at any offset (LL's fallback for services that scrub headers); b9 keeps Firestorm's refusal at a later offset, and the comment now says so | +7 −6 | built |

Total step (b): +285 −274 in 16 files (b6 removes more than it adds; b8–b10 are the review follow-ups).

### The pipelining replacement (b6, and the same in `vkstorm-http`)

- **One rule for in-flight requests.** `HttpPolicyClass::activeLimit()`:
  a deep-queue class (pipelining depth > 1: `AP_TEXTURE`, `AP_MESH2`,
  `AP_ASSET` with `HttpPipelining` on) runs per-host limit × depth requests,
  other classes their connection limit. The policy (what llcorehttp hands to
  curl) and the transport (what curl may open) both use it, so curl runs
  every request it is given and none waits in curl's pending queue with its
  `CURLOPT_TIMEOUT` running (the study's risk 3).
- **Deep-queue multi handles:** `CURLMOPT_PIPELINING = CURLPIPE_MULTIPLEX`,
  `MAX_HOST_CONNECTIONS = MAX_TOTAL_CONNECTIONS = activeLimit()`,
  `MAX_CONCURRENT_STREAMS = depth` (curl 7.67+). Each of their requests sets
  `CURLOPT_PIPEWAIT`, so on an h2 server curl waits for a free stream on an
  open connection (up to `depth` per connection: the shape pipelining had)
  instead of opening another. On HTTP/1.1 (the asset CDN, port 80) every
  request has its own connection. Other classes: `CURLPIPE_NOTHING`,
  limits as before.
- **In-flight counts unchanged** (`PIPELINING_DEPTH` stays 5): 40 texture,
  40 mesh2, 60 asset requests, but now on up to 40/40/60 HTTP/1.1
  connections to the CDN instead of 8/8/12 pipelined ones. That is the
  study's mitigation for risk 1 (match today's in-flight counts); its risk 2
  (CDN limits on connections) and gate G3 (measured load times) decide the
  final number, and `PIPELINING_DEPTH` is the one knob (3 gives 24/24/36,
  the study's suggested starting range).
- **Removed:** the stall that waited for a class to go idle before changing
  options (it protected curl 7.37's pipelining switch); Firestorm's X-LL-URL
  and Content-Range desync check, which answered a mismatched reply with a
  503 and switched pipelining off on every class (with it gone, LL's
  BUG-2295 expectation, deliver a longer 206 than asked for, holds again;
  its offset test comes back on its own in b8 and b9: a 206 that starts
  elsewhere than asked, or that carries no usable Content-Range for a
  request at a later offset, is retried and then fails as before, which
  leaves the texture at the data it has. LL's texture and mesh fetchers
  would take such an unlabelled reply as the asked range, and with
  pipelining gone it can no longer be a desync, so relaxing that case is
  an option if a grid's services scrub Content-Range; the prototype keeps
  Firestorm's behaviour);
  the `VIEWERASSET` environment switch to `CURL_HTTP_VERSION_2_0`. curl 8's
  default negotiates h2 over TLS only; it would have tried h2c upgrades on
  the CDN's `http:` URLs.
- **Kept:** the doubled transfer timeout for deep-queue classes. The study
  asked for it to be re-examined: with pipelining, a request waited behind
  others on its connection; now the requests share the link side by side,
  so on a slow link the last one still finishes no sooner than the last of
  a pipeline did. Dropping the doubling would recover sooner from a stalled
  CDN (BUG-7698's concern) but risk spurious timeouts on slow links; without
  a measurement the existing balance is kept.
- **Texture and mesh water marks** still key off `isPipelined()` and
  `PIPELINING_DEPTH`: unchanged.

### The Accept-Encoding guard (decision: kept; only the tests change)

Firestorm compiles `CURLOPT_ENCODING ""` out for curl 7.81 and later on
every method but POST (`_httpoprequest.cpp:521-523`, and the template
handle in `httpcommon.cpp:294-297`), because AWS sends `Content-Encoding:
binary/octet-stream`. In curl 8.23 that header is processed only when
decoding is on (`lib/http.c:3335`), and an unknown encoding then fails the
transfer ("Unrecognized content encoding type", `lib/content_encoding.c`
`error_writer`): verified by reading the source. With curl 8 the guard does
what it says: no `Accept-Encoding` on GET and PUT, `deflate, gzip` on POST
(verified by the restored llcorehttp header tests, which needed b7, and by
the Rust GET scenario). Kept, because re-enabling it risks failing asset
GETs from S3-backed origins, and the assets are already compressed. Cost:
LLSD replies to GET (AIS) are no longer compressed; measure their size in
G3. POST stays as it is (its replies come from simulators and login).

### Results (verified)

- **Build** after b1–b7: RelWithDebInfo, LTO, `LL_TESTS=TRUE`: 0 errors,
  `EXIT 0`; staged as before. The rebuild after switching the package and
  applying b1–b6 took 15 min 21 s (every `curl.h` includer recompiled, every
  libcurl user relinked); the final incremental build 2 min 24 s.
- **Tests:** every test executable that links libcurl was relinked and
  rerun against curl 8.23.0: 82 executables, 1198 tests: 1187 passed,
  9 skipped known failures, 2 failed (the llcorehttp header tests, fixed
  by b7). `INTEGRATION_TEST_llcorehttp` after b7: 63 tests, 60 passed
  (BUG-2295 included), 3 skipped known failures (LL's FIXMEs 19–21), 0
  failed. `INTEGRATION_TEST_llsechandler_basic`: 11/11 again.
- **After the review follow-ups** (a7, b8): incremental build 10 min 31 s,
  0 errors; 82 test executables relinked and rerun, 1199 tests: 1190
  passed, 9 skipped known failures, 0 failed; `llsechandler_basic` 12/12;
  llcorehttp 63/60/3 with the refused 206 logged ("Partial content at
  offset 0 for a request at offset 10").
- **After the second follow-up** (b9, and b10, a comment): incremental
  build at b10 8 min 57 s, 0 errors, 0 warnings, staged; 82 test
  executables relinked and rerun with TEMP in `.fixture-tmp`: 1199 tests,
  1190 passed, 9 skipped known failures, 0 failed; `llsechandler_basic`
  12/12; llcorehttp 63/60/3, with both refusals logged (`/bug2295/0/` and
  `/bug2295/no_range/`, each "Partial content at offset 0 for a request
  at offset 10", once more on the retry). The smoke test was not repeated
  after b8–b10, which change only how a 206 at an unexpected offset ends.
- **Smoke test** (same harness and scratch profile; real store unchanged):
  login screen after 8 s; `libcurl version is: libcurl/8.23.0 OpenSSL/3.5.9
  zlib/1.3.1.zlib-ng nghttp2/1.64.0`; `GridListRequest: got new list` from
  the HTTPS grid list, with peer and host verification through the viewer's
  certificate store; no TLS, certificate or curl error; FS data over HTTP as
  before. Not logged in; terminated after the same 60 s `WM_CLOSE` wait.
- **CDN fetch through llcorehttp** (`http_texture_load`, same 8 textures):
  `-p 5 -c 8` (deep queue: up to 40 in flight, one connection each): 8
  successes, 238,211 bytes, 0 errors, 0.45 s; `-p 0`: 8 successes, 0.25 s.
  Single runs on 8 small textures: a smoke test, not the G3 comparison.
- **Not run:** a Linux build (the two Linux fixes were checked with clang
  only); a login, so neither certificate dialog is seen on screen (the
  transport and listener half of G4 is now run: see the follow-up); curl's
  own test suite; any load-time measurement.

## Rust core: `vkstorm-core-rust`, branch `codex/curl8-prototype`

Worktree `vkstorm-core-rust\work\curl8-prototype` (was `vkr\core-curl8-prototype`), from `main` at `08a5cbd`. Pairs with
step (b) only: step (a) keeps curl 7.54.1 and needs no Rust change (the core
uses no OpenSSL; `Cargo.lock` has no TLS crate).

| Commit | Change | Status |
|---|---|---|
| `8b6c519` | `vkstorm-http`: the C++ b6 change, one to one. `ClassOptions::active_limit()` used by the policy and the transport; deep-queue multi handles get `CURLPIPE_MULTIPLEX`, host and total connections = active limit, `MAX_CONCURRENT_STREAMS` = depth; requests set `CURLOPT_PIPEWAIT`; options apply at once (stall and `dirty` gone); the X-LL-URL/Content-Range desync check and `RequestOp::xll_url` gone; the doubled transfer timeout kept; `VIEWERASSET` switch gone. New bindings: `CURLOPT_PIPEWAIT`, `CURLMOPT_MAX_CONCURRENT_STREAMS`, `CURLPIPE_NOTHING`, `CURLPIPE_MULTIPLEX`. New tests: `active_limit` unit test; scenario `deep_queue_runs_per_host_times_depth_on_own_connections` (per-host 2 × depth 3: six requests in flight, never two at once on one connection, at most six connections). `status_strings` accepts curl 8's "Could not connect to server" | verified |
| `a77ca42` | Request options as the C++ sets them when built against curl 8: no `Accept-Encoding` except on POST (the Firestorm guard above), If-Modified-Since through `CURLOPT_TIMEVALUE_LARGE` (64-bit `curl_off_t`; `CURLOPT_TIMEVALUE` is a 32-bit `long` on Windows). The GET scenario now expects no `Accept-Encoding` | verified |
| `586b1cf` | README: the `link-curl` library list for the new packages (`bcrypt;iphlpapi`; no `wldap32`, `normaliz`) | verified (the list links) |
| `bd267c3` | Scenario `options_change_under_active_requests`: a deep-queue class switched to depth 0 with six requests in flight; all of them and eight later ones complete, the later ones within the new limit. Answers the Copilot review's "active-transfer reconfiguration" note (the stall's removal had no test) | verified (3 runs) |
| `42eb1c8` | The b8 offset check, one to one; mock endpoint `/fixedrange/`; `odd_bodies` checks a refused 206 at the wrong offset and a delivered one at the asked offset | verified |
| `7748f1c` | The b9 change, one to one (from this PR's Copilot review): the length guard goes, so a 206 without a usable Content-Range is refused for a request at a later offset and delivered at offset 0. Mock endpoints `/norange/` (no Content-Range) and `/oddrange/` (`items 0-9/100`, which the parser leaves alone); `odd_bodies` checks both at offsets 20 and 0 | verified |
| `2798df6` | The b10 comment, one to one | comment only |

Not changed (the study's optional items): `CURLINFO_*_DOWNLOAD` doubles
(no deprecation warnings in Rust), `vkstorm-audio`'s private curl bindings
(every constant exists in curl 8).

**Tests** (TEMP/TMP in `.fixture-tmp`):

- `cargo test -p vkstorm-http --features link-curl` against the curl 8.23.0
  + OpenSSL 3.5.9 static libraries: 16 unit tests and 17 scenarios pass,
  three runs in a row. With the follow-ups (`bd267c3`, `42eb1c8`,
  `7748f1c`) there are 18 scenarios; at `2798df6`, three runs in a row
  again, 16 + 18 each, with another viewer build running in parallel. One
  earlier run under full CPU load (a viewer build in parallel) failed
  `concurrency_limits_and_throttle` once on timing ("sixth throttled
  request after 870 ms", window 900–1400 ms); throttling is not touched by
  these commits.
- The same suite at the first commit against curl 7.54.1 + OpenSSL 3.5.9:
  16 + 17 pass. With `CURLPIPE_MULTIPLEX` and no HTTP/1.1 bit, 7.54.1 does
  not pipeline either, so the new scenario holds there too
  (`CURLMOPT_MAX_CONCURRENT_STREAMS` is unknown to 7.54.1 and only logs a
  warning). The second commit needs curl 7.59+ (`TIMEVALUE_LARGE`).
- `cargo test -p vkstorm-audio --features link-curl` against curl 8.23.0:
  114 passed, 2 ignored (parcel audio streams, ICY through
  `CURLOPT_HTTP200ALIASES`).
- `cargo test -p vkstorm-http` (no libcurl): 16 pass. `cargo fmt --check`
  clean; `cargo clippy -p vkstorm-http --all-targets --features link-curl`
  clean. `cargo test --workspace --locked --no-fail-fast`
  (CI's command) at `2798df6`: 446 passed, 0 failed, 2 ignored in 83 test
  binaries.
- Not in CI: the `link-curl` suites still do not run in the core's CI
  (`.github/workflows/test.yaml`), as the study found.

**Parity fixture.** The core has no fixture that encodes pipelining. The
viewer's rust line does: `indra/llcorehttp/tests/fixtures/llcorehttp_parity.xml`
(recorded from the C++ before the port) holds a `pipelining` scenario (one
connection, overlap 1 under 7.54.1 pipelining), `dynamic_pipelining`, and
`Accept-Encoding: deflate, gzip` in every GET's raw request head. With curl 8
those change on both sides, so the fixture must be re-recorded: run the
rust line's `llcorehttp_parity_test` and its mock server against a C++
llcorehttp that carries b1–b10 (this prototype tree, for example) with
`VKS_HTTP_RECORD=<file>`, then compare the Rust service against it on the
rust line. Not done here (it belongs to the rust line's integration PR).

## Per-line notes

Checked by applying the whole series, in order, to each line's tree in a
temporary index (`git read-tree origin/<line>` then `git apply --cached`
commit by commit; no checkout): release `8617c997a7`, canary `307df79dd4`,
rust `e89ee30f6e`, aleph `1678d1a92c`, fetched 2026-11-05 08:13 UTC.
"Applies" means as-is; nothing needed a 3-way merge. Of 68 checks (17
code commits, 4 lines), 50 apply as-is; **vkstorm-release takes all 17
commits unchanged**.

Per file, the series' net change (`a5290e8c81..` the branch) against each
line's copy: on release every touched file is identical to the series
base. On canary and aleph only `indra/llcorehttp/CMakeLists.txt` (a6) must
be placed by hand; `linux_crash_logger.cpp`, `test_httprequest.hpp` and
`llsechandler_basic_test.cpp` differ there but take the net change as-is,
and the llcorehttp internals are identical to the base. Commit by commit,
a6's BUG-2295 skip does not apply to their `test_httprequest.hpp`, so
every later commit with a hunk in that file (b6–b9) is marked too, as is
b10, which edits b9's lines. On rust: `CMakeLists.txt` and the seven
llcorehttp internals that the line replaced with the Rust service.

| Commit | release | canary | rust | aleph |
|---|---|---|---|---|
| a1 ENGINE/init (`llsecapi.cpp`) | applies | applies | applies | applies |
| a2 `get0` dates | applies | applies | applies | applies |
| a3 credential store | applies | applies | applies | applies |
| a4 credential store tests | applies | applies | applies | applies |
| a5 crash logger locking | applies | applies | applies | applies |
| a6 llcorehttp test registered | applies | adapt: the commented block sits after `wsserver`/`jsonrpcws` in another order; same 7 lines | adapt: the rust line already registers `INTEGRATION_TEST_llcorehttp` (and a parity test) against the Rust service; keep only the BUG-2295 skip if its desync check fails it there too (the Rust port has the same check) | as canary |
| a7 store read through `istream::read` | applies | applies | applies | applies |
| b1 `CURL.cmake` | applies | applies | applies | applies |
| b2 `CURLINFO_*_T` | applies | applies | adapt: `_httpoprequest.cpp` is gone (Rust service); the Rust transport reads the doubles, optional to change | applies |
| b3 crash logger mime | applies | applies | applies | applies |
| b4 certificate kind | applies | applies | applies (the rust line's `httprequest.cpp` bridge calls the same `sslVerify` and keeps its full `HttpStatus`, error data included, for the reply: read, not run) | applies |
| b5 SSL message | applies | applies | applies | applies |
| b6 pipelining | applies | adapt: only the BUG-2295 hunk, since a6 was adapted; the code applies | adapt: the C++ internals are gone; the change is the Rust core branch (`8b6c519`, `a77ca42`) plus the `httprequest.h`/`llappcorehttp.cpp` comments; the parity fixture must be re-recorded (above) | as canary |
| b7 header tests | applies | adapt (follows a6) | applies (the Rust transport sends no `Accept-Encoding` on GET after `a77ca42` either) | as canary |
| b8 206 offset check | applies | adapt: the test hunk follows a6; the `_httplibcurl.cpp` hunk applies | adapt: done in Rust (`42eb1c8`) | as canary |
| b9 206 without Content-Range | applies | adapt: the test hunk follows a6; the `_httplibcurl.cpp` and peer hunks apply | adapt: done in Rust (`7748f1c`) | as canary |
| b10 comment | applies | applies on top of b9 (marked only because b9 was held back whole) | adapt: done in Rust (`2798df6`) | as canary |

Packages: all five lines pin the same `openssl` (1.1.1w-r4) and `curl`
(7.54.1-r4) entries, so one `autobuild.xml` change per line once the
packages are published. canary and aleph follow upstream Firestorm, which is
still on 1.1.1w and 7.54.1: their syncs will conflict in `autobuild.xml` and
in the changed hunks (inferred from the OpenSSL study's survey).

## Effort

What was hit here, against the studies' estimates:

| Item | Studies | Here | Remaining |
|---|---|---|---|
| OpenSSL 3.5 package | 1–2 d | first try built and passed its 3822 tests (22 min); 0.5 d with the fork | linux64 in CI: 0.5–1 d (not run) |
| curl 7.54.1 rebuild | 1–3 d (risk of source patches) | no patch; 2 min | linux64 in CI: 0.5 d |
| curl 8.23 package | 2–3 d | one CMake fix (FindOpenSSL on MSVC); 0.5 d | linux64 in CI, curl's own HTTP/TLS tests: 1 d |
| Step (a) viewer change and tests | 1.5–2 d | one build error, the credential store, two new tests: done; the credential store is now on every line through the hotfix, and its OpenSSL 3 branch was built and tested here against 3.5.9 | Linux `-Werror` build (CI), a real profile with saved SL/OpenSim logins and MFA through upgrade and downgrade: 0.5–1 d |
| Restore llcorehttp tests | 1–2 d | registered and passing in both steps; 2 tests needed the Accept-Encoding expectation, 1 the desync check: done | rust line adaptation: 0.25 d |
| Step (b) compile/link fixes, certificate kind | 2–3 d | one link fix, two Linux fixes, certificate kind: done | transport half of G4 done (the probe); the dialogs need the finding-3 hotfix first, then one look at each dialog with the probe's servers: 0.5 d |
| Pipelining replacement, C++ and Rust | 4–6 d | done, tested against the mock servers | parity fixture re-record on the rust line: 0.5–1 d |
| G1 baseline trace, G3 measurement harness and tuning | 6–8 d | not started (needs in-world sessions) | 6–8 d |
| Server tests (G4), CI, per-line integrations | 3–4 d | per-line check done (mostly applies as-is) | 3–4 d |

Updated totals (follow-up): **OpenSSL step about 4–6 working days** left
(the credential store is done on every line), **curl 8 step about 11–15
days**, plus the finding-3 hotfix (about 0.5 d per line set, a separate
release patch). First pass: **OpenSSL step about 5–7 working days** left (was 8–12),
because the packages built first time and 7.54.1 needed no patch; **curl 8
step about 12–16 days** left (was 16–24), most of it the measurement work
(G1, G3), which this prototype does not touch.

## Findings beyond the studies

1. **The credential-store breakage is invisible to the compiler**
   (verified): `EVP_rc4()` compiles and links against 3.5; only the run
   time fails. A build-only gate would have shipped the store loss.
2. **A third way to lose the store** (verified by test 11): the old write
   path never checked the temporary file's stream; a failed or partial
   write (disk full, locked file, a directory in the way) went on to remove
   the store and rename. Fixed with the other two.
3. **The certificate dialogs cannot show the certificate today**
   (inferred from code, pre-existing, not changed here): llstartup calls
   `gSecAPIHandler->getCertificate(response["certificate"])`
   (`llstartup.cpp:1925`, and `trust_cert_done` at `:4690`), but the
   certificate data is the LLSD map from `LLBasicCertificate::getLLSD`
   (subject, issuer, dates; no PEM); the implicit `LLSD` to `std::string`
   conversion gives `""`, `LLBasicCertificate("")` throws
   `LLInvalidCertificate`, and the user gets `GeneralCertificateErrorShort`.
   b4 makes the dialog choice right; making either dialog work needs the PEM
   in the certificate data (a follow-up, with the G4 test).
   *Follow-up:* the missing PEM is now **verified** on both the release
   viewer and this prototype (the certificate probe's replies carry no
   `pem`), so no line can show `TrustCertificateError` or
   `GeneralCertificateError` today. A pre-existing bug: a hotfix candidate
   for release and every line, outside this prototype.
4. **The asset CDN's HTTPS certificate does not match its name** (verified
   with curl 8.23: "no alternative certificate subject name matches target
   hostname 'asset-cdn.glb.agni.lindenlab.com'"), which is why the viewer's
   texture, mesh and asset traffic is plain HTTP on port 80 (the study's
   gate G1 question, answered for the scheme and port).
5. **OpenSSL 3.5 negotiates post-quantum key exchange with SL's login
   server** (verified with curl 8.23: TLS 1.3, `X25519MLKEM768`); 7.54.1
   uses OpenSSL's default groups too, so the same is expected there
   (inferred).
6. **curl 7.54.1 misreports OpenSSL 3** as `OpenSSL/3.5.0i` (verified),
   cosmetic; a one-line patch in `Curl_ossl_version` (use
   `OpenSSL_version(OPENSSL_VERSION)`) if it matters for the About box.
7. **libcurl's error texts changed** ("Couldn't" to "Could not"), visible
   in `HttpStatus::toString()` and logs; no viewer code matches on them
   (verified by search); one Rust test did.
8. **LL's llcorehttp tests disagree with two Firestorm behaviours**
   (verified): BUG-2295 with the pipelining desync check, and the GET/PUT
   header tests with the Accept-Encoding guard under curl 7.81+.
9. **OpenSSL configuration file** (inferred, unchanged): curl 7.54.1 loads
   the default `openssl.cnf` explicitly (`CONF_modules_load_file`), curl 8
   through `OPENSSL_init_ssl(OPENSSL_INIT_LOAD_CONFIG)`, and libssl
   autoloads it anyway, from `OPENSSL_CONF` or the compiled-in OPENSSLDIR
   (admin-only on Windows). That is today's behaviour too. To make the
   viewer independent of a machine's `openssl.cnf`, build OpenSSL with
   `no-autoload-config` and curl with `CURL_DISABLE_OPENSSL_AUTO_LOAD_CONFIG=ON`;
   not done in the prototype. *Follow-up, verified:* the viewer binary holds
   `OPENSSLDIR: "C:\Program Files\Common Files\SSL"` (admin-writable only),
   the same as the package's `openssl version -d`. A build made outside the
   package script keeps its build-time prefix instead (the standalone
   `openssl-3.5\install` build names a user directory, which no longer
   exists after the move), so the package script's `--openssldir` matters.
10. **Build time**: a full RelWithDebInfo + LTO + tests build here is about
    half an hour of work, but the machine sleeps when idle; one build took
    3 h 44 min of wall clock for that reason.

## Follow-up 2026-11-05: gaps closed

A second pass over the brief against the first one. What was missing or
only reasoned, and what was done about it:

| Gap | Done here | Status |
|---|---|---|
| The certificate failure at login (b4, b5; the study's G4) was built but never run | Run through the real login transport, without logging in, on this build and on the installed release viewer (below) | verified, up to the dialog llstartup picks |
| The smoke test was not repeated after b8–b10 | Repeated at b10 (and again after the hotfix cherry-pick, below) | verified |
| The credential-store hotfix `68ec9057a3` landed on every line after this branch: the a3/a4/a7 patch set no longer applied, and the hotfix's OpenSSL 3 branch had not been built against OpenSSL 3 | Cherry-picked (`528cba6c39`), a2 kept on top; rebuilt and tested against OpenSSL 3.5.9 (below) | verified |
| Per-line notes were taken at 08:13 UTC; the lines moved since | Redone on the current tips (below) | verified |
| The worktree and packages moved (`vkr\`, `repos\work\` to `vulkanstorm\work\`) | `my_autobuild.xml` URLs and the build directory updated | done |
| Linux builds | Still not run: no WSL or container runtime on this machine (`wsl --status`: not installed) | open |
| curl's own test suite (G0), the real-store credstore check (`CREDSTORE_REAL=1`, reads the owner's store), a real profile through upgrade and downgrade, G1/G3 load measurements | Not done: each needs either the owner's data, a login, or a Linux/MSYS test host | open |

### Certificate failures at login: the probe

`scripts/tests/tls_cert_probe/run_probe.ps1` (new, this branch) makes a
throw-away PKI with OpenSSL 3.5.9, serves five local HTTPS endpoints
(Python `ssl`, 127.0.0.1 only), and starts the staged viewer with an empty
scratch profile whose `CA.pem` trusts the test CA. A LEAP plugin
(`xmlrpc_cert_probe.py`, loaded through a `--sessionsettings` file, because
`--leap` on the command line needs LLSD notation quoting) posts one XML-RPC
request with no credentials to each endpoint through the
`LLXMLRPCTransaction` pump. That is the transport the login uses: the same
`LLXMLRPCTransaction`, the viewer's `sslVerify` and certificate store, and
the `LLXMLRPCListener` that fills the reply llstartup reads (`status`,
`errorcode`, `certificate`). From those three values llstartup's branch is
fixed (`llstartup.cpp:1916-1961`); the script prints it. The real profile
is never opened; the SHA-256 of `bin_conf.dat` was the same before and after
every run.

| Endpoint | Release viewer (OpenSSL 1.1.1w, curl 7.54.1) | This prototype (OpenSSL 3.5.9, curl 8.23.0) |
|---|---|---|
| valid (test CA in `CA.pem`) | `Complete` | `Complete` |
| expired leaf, trusted CA | `SSL_PEER_CERTIFICATE`, no certificate: generic login error, "Despite our best efforts..." | `SSL_PEER_CERTIFICATE`, certificate, `invalid`: certificate error, `ssl_connect_error` text |
| leaf from an unknown CA | `SSL_CACERT`, certificate (the CA's): certificate error | `SSL_CACERT`, certificate, `untrusted_ca`: certificate error |
| self-signed leaf | `SSL_CACERT`, certificate | `SSL_CACERT`, certificate, `untrusted_ca` |
| wrong host name (curl's own check) | `SSL_PEER_CERTIFICATE`, no certificate: generic login error | `SSL_CACERT` (curl's 60, named by the listener's code table), no certificate: generic login error with the `ssl_connect_error` text |

What this shows:

- b4 works on curl 8: the validator's verdict travels with the certificate,
  and only an unknown CA (or a self-signed leaf) is named `SSL_CACERT`. An
  expired certificate under a trusted CA is `SSL_PEER_CERTIFICATE` and now
  carries its certificate, so llstartup takes the certificate branch where
  release showed the generic text. Without b4 that case would have been
  curl 8's 60, named `SSL_CACERT` with the certificate attached: the
  "trust this CA" branch (inferred; not built without b4).
- b5 works: every TLS certificate failure gives the `ssl_connect_error`
  text on curl 8.
- The viewer's own validator classifies the same on OpenSSL 3.5.9 as on
  1.1.1w (`CertExpired`, the trust exception for an unknown or self-signed
  root). Step (a) keeps curl 7.54.1, whose codes 51 and 60 differ, so its
  reply is the release column (inferred: the step (a) build was not probed
  separately; its validator is this one, its curl is release's).
- **Finding 3 confirmed on both viewers:** the certificate data in the reply
  is the `getLLSD()` map (subject, issuer, dates, extensions, now
  `validation_failure`) with no PEM, so llstartup's
  `getCertificate(response["certificate"])` gets `""`, throws, and shows
  `GeneralCertificateErrorShort`. Neither `TrustCertificateError` nor
  `GeneralCertificateError` can appear on any line today. The probe proves
  the missing PEM; the throw on `""` is read from the code. This is a
  pre-existing bug, so by the project's rule it is a hotfix on release and
  every line, not part of this prototype; b4 is what makes the dialog choice
  right once the PEM is there.
- Public test hosts are not a usable gate: `badssl.com`'s
  `untrusted-root`, `self-signed` and `wrong.host` reset connections
  intermittently from here (curl 8 and Windows' own curl alike, with `-k`
  too), which first looked like a curl 8 regression (`SSL_CONNECT_ERROR`).
  The local servers make the probe deterministic.

Run it with:

```
pwsh -File scripts\tests\tls_cert_probe\run_probe.ps1 -Exe <staged or installed viewer exe> -OpenSsl <openssl.exe 3.4+> -WorkDir <scratch dir> -Python <python with the llsd package>
```

Bounded: the servers exit after 200 s, the viewer is closed once the result
is written (or after 180 s) and killed 30 s later. Two runs on each viewer
gave the same table.

### Rebuild with the hotfix (`528cba6c39`), OpenSSL 3.5.9 and curl 8.23.0

Fresh configure of the moved build directory, then `autobuild build -A 64
-c RelWithDebInfoFS_open --no-configure` (RelWithDebInfo, LTO,
`LL_TESTS=TRUE`): 42 min 33 s, 0 errors, exit 0, staged. The tests the build
runs: 93 test executables, 1333 tests passed, 9 skipped known failures
(the six from step (a), mathmisc's being `LLSphere` 2, and llcorehttp's
FIXMEs 19–21), 0
failed. `INTEGRATION_TEST_llsechandler_basic`: 12/12, so the hotfix's
OpenSSL 3 branch (private `OSSL_LIB_CTX`, legacy provider,
`EVP_CIPHER_fetch`) builds and passes against 3.5.9, the textbook-RC4
format check (test 10) and the failed-write and unreadable-store cases
(11, 12) included. `INTEGRATION_TEST_llcorehttp`: 63 tests, 60 passed, 3
skipped. The certificate probe on this build gave the same table as
above; smoke: `GridListRequest: got new list`, `STATE_LOGIN_WAIT` 5 s after
start, no TLS or curl warning outside the probe's endpoints.

### Smoke test at b10, repeated

Staged viewer, scratch profile: `libcurl/8.23.0 OpenSSL/3.5.9
zlib/1.3.1.zlib-ng nghttp2/1.64.0`, `GridListRequest: got new list` over
HTTPS, `STATE_LOGIN_WAIT` reached in 4 s; no `CoreHttp`, TLS or certificate
warning other than the probe's own endpoints. Not logged in.

### Per-line check on the current tips

Same method as [Per-line notes](#per-line-notes), on release `9f58734096`,
canary `aee351a302`, rust `fb40f493a4`, aleph `95d5446a58` (fetched
2026-11-05 ~12:30 UTC), with a3, a4 and a7 left out because each line has
the hotfix. Release takes a1, a2, a5, a6 and b1–b10 as-is. Canary and
aleph: a6 must be placed by hand, and b6, b8, b9, b10 then do not apply
as-is (b6 and b9 for their `test_httprequest.hpp` hunks after a6, b8–b10
for `_httplibcurl.cpp` hunks that follow b6); b7 now applies as-is there
(it was marked "adapt" before). Rust: a6 adapted, b2, b6, b8–b10 are the
Rust core's (their C++ files are gone), the rest applies. a2 applies as-is
on top of the hotfix on all four lines.
