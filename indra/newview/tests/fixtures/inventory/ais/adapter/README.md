# ais/adapter: the AIS reply adapter corpus (recorder R3a, the HTTP part)

What `LLCoreHttpUtil::HttpCoroutineAdapter`'s LLSD handler
(`HttpCoroLLSDHandler`, `indra/llmessage/llcorehttputil.cpp`) makes of an AIS
reply: the LLSD it posts to the coroutine and where the body was parsed. The
AIS update corpus (`../update`, `INTEGRATION_TEST_llaisupdate_parity`)
starts where this one ends: its scripted results are what this handler
produces. Together they cover the AIS reply path from the HTTP response to
the inventory model.

Recorded from the C++ with the AIS depth cap (an approved fix, 2026-10-29):
AIS sets `AIS_MAX_REPLY_DEPTH` (256, `llaisapi.cpp`) on its adapter, so a
value nested 256 containers deep makes the body unparsable, as malformed XML
does. The C++ XML parser never limited depth; its Rust implementation
(`vkstorm-llsd`) now applies `max_depth` like the binary and notation
parsers, and the viewer's other XML callers keep passing -1 (unlimited).
`responseToString`, which the handler calls to log an unparsable
`application/llsd+xml` body, parses with the same limit (without it, a
100,000-level body crashed the 499 path while being logged).

The fixtures are never edited by hand: change the test and dump again.

## Files

| path | what |
|---|---|
| `expected.llsd` | binary LLSD: an array with one record per case, in run order |
| `expected.txt` | review rendering: a header (viewer commit, platform, case count), then one record per line in notation |

A record is `{id, status_type, status_status, body_size, path, result}`:

* `id`: `<status>/<body>/<content type>`;
* `status_type`, `status_status`: the `LLCore::HttpStatus` of the response;
* `path`: `main` (parsed in `onCompleted`), `worker` (posted to the
  `General` work queue and back to `mainloop`: success bodies over 64 KB,
  `MAX_BODY_SIZE_THRESHOLD`), or `none` (nothing posted: the reply to a
  canceled request, `HE_HANDLE_NOT_FOUND`);
* `result`: the LLSD posted to the coroutine's pump, `http_result` included.
  Strings and binaries over 256 bytes are recorded as
  `<N bytes, fnv1a64 H>` (FNV-1a, 64 bits, of the bytes).

## The matrix

Every combination of:

* **status** (16): HTTP 200, 201, 204, 206, 299 (success), 300, 400 (with a
  message), 403, 404 (with a message), 410, 499 (from the server), 500 (with
  a message), 503; curl error 28 (`EXT_CURL_EASY`, timed out); `LLCORE`
  `HE_OP_CANCELED`; `LLCORE` `HE_HANDLE_NOT_FOUND`;
* **body** (18): no body buffer, an empty one, an LLSD map, a map with
  `item_id` (an error body), a map holding its own `http_result` (the
  adapter's replaces it), an array and a string (moved under `content`), an
  undefined value, an unterminated document, plain text, JSON, maps and
  arrays nested 255 deep (parses), 256 deep (refused by the cap) and 100,000
  deep (refused); and over 64 KB: a padded map, a padded array, an
  unterminated padded document, and the 256-deep body padded with trailing
  whitespace;
* **content type** (6): none, `application/llsd+xml`,
  `application/llsd+xml; charset=utf-8`, `APPLICATION/LLSD+XML`,
  `application/xml`, `text/plain`.

Each response also carries `X-Vks-Test: 1` and an empty `X-Vks-Flag:`
header (recorded as `true`), the request URL and method `GET`. 1,728 cases.

What it shows (each a case to keep when the handler is replaced):

* the 499 replaces a success status only for an unparsable body whose
  content type is exactly `application/llsd+xml` (the charset and
  upper-case variants keep 200 and an empty map);
* an empty or absent body on success gives `{content: undefined}`; a
  non-map body moves under `content`; an error status keeps only a 4xx
  body (a map becomes the result, another value goes under `content`),
  and adds `error_body` (the whole body as a string) to `http_result`, also
  after a 499;
* the adapter's `http_result` replaces a body's own;
* header names are lower case (`HttpHeaders::appendNormal`), an empty value
  reads `true`;
* success bodies over 64 KB parse on the worker queue; error bodies and
  everything else on the calling thread.

## Platforms

Recorded on Windows (MSVC, RelWithDebInfo). Linux (GCC) dumps are made by
the `record_inventory_fixtures` input of the Build workflow; they must be
identical in `expected.llsd` and in every `expected.txt` line but the
header. The CI dump (run 41253303267, GCC on Ubuntu 24.04) is: one tree
serves both standard libraries.
