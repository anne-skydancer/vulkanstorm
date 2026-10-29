# 02 — Networking and messaging

This chapter covers how the viewer talks to the grid: the UDP message system
in `indra/llmessage` (circuits, reliability, the template codec, xfer and the
transfer manager, trusted circuits, the `LLHTTPNode` tree that dispatches
event-queue messages), the HTTP layer `indra/llcorehttp` and its coroutine
helpers, and the viewer glue in `indra/newview` (login, the region seed
capability and capability table, the event poll, the message handler
registry, AISv3, teleport and region crossing). Two rungs of the Rust port
already sit in this layer: the template codec and data packers (rung three,
`vks_msg_*`/`vks_dp_*`) and the HTTP service and transport (rung four,
`vks_http_*`). Everything around them (sockets, `LLCircuit` reliability,
dispatch, xfer, the transfer manager, coroutines, capabilities, the event
poll and every handler) is still C++. For the inventory and object-store
rungs this layer is a boundary to keep, not a thing to port first: the
handlers those rungs replace are entries in a single C++ dispatch table and a
set of capability coroutines, and the existing recorders already drive them
through the real C++ message system.

Paths without a leading directory are relative to `indra/`; `doc/`,
`scripts/` and `.github/` paths are relative to the repository root. Line
numbers refer to branch `codex/rust-inventory-model` (HEAD `8d101eccb6`) as
surveyed on 2026-10-27. Rust crate paths refer to the sibling checkout of
`vkstorm-core-rust` at `1b183c7`.

## Scope and size

Line counts are physical lines of `.cpp`/`.h` files, measured from `indra/`
with

```sh
f=$(ls <files>); echo "$f" | wc -l; cat $f | wc -l
```

(the file sets are the globs listed in the "Files" column; every file in
`llmessage/` and `llcorehttp/` top level is in exactly one group).

### `llmessage/` (161 files, 53,936 lines)

| Group | Files (globs) | Files | Lines | Main subareas |
|---|---|---:|---:|---|
| UDP core | `message.{cpp,h}` `llcircuit.*` `llpacket*.*` `net.*` `llhost.*` `llthrottle.*` `llmessagethrottle.*` `llmessageconfig.*` `llstoredmessage.*` `llmessagesenderinterface.h` `llgenericstreamingmessage.*` `machine.*` `llxorcipher.*` `llnullcipher.*` `llcipher.h` | 32 | 10,869 | `LLMessageSystem` send/receive loop, circuits, acks and resends, packet ring with SOCKS5 wrapping, Winsock/BSD sockets, throttles, `message.xml` config |
| Template codec | `llmessagetemplate*.*` `lltemplatemessage*.*` `llsdmessage*.*` `llmessagebuilder.*` `llmessagereader.*` `message_prehash.*` `message_string_table.cpp` `lldatapacker.*` `llmsgvariabletype.h` | 24 | 8,616 | Template parser, builder, reader (Rust-backed), LLSD builder/reader (C++), prehashed names, data packers (Rust-backed) |
| Xfer | `llxfer*.*` | 10 | 3,389 | `LLXferManager`, file/mem/vfile xfers |
| Transfer manager | `lltransfer*.*` | 10 | 3,017 | `LLTransferManager`, channels, asset/file sources and targets |
| Asset storage | `llassetstorage.*` | 2 | 1,921 | `LLAssetStorage` (bookkeeping Rust-backed since rung five) |
| Coroutine HTTP | `llcorehttputil.*` `fscorehttputil.*` `llcoproceduremanager.*` | 6 | 2,925 | `HttpCoroutineAdapter`, LLSD/raw/JSON handlers, coprocedure pools; `fscorehttputil` is Firestorm |
| HTTP node / services | `llhttpnode*.*` `llhttpsdhandler.*` `lliohttpserver.*` `llsdhttpserver.*` `llsdappservices.*` `llservice.*` `llservicebuilder.*` `lltrustedmessageservice.*` `lldispatcher.*` | 19 | 3,765 | `LLHTTPNode` tree (`/message/<name>`), trusted message service, `LLDispatcher` (generic/estate sub-dispatch) |
| IO pipes | `llpumpio.*` `lliopipe.*` `lliosocket.*` `llbuffer.*` `llbufferstream.*` `lliobuffer.*` `llchainio.*` `llioutil.*` | 16 | 5,844 | `LLPumpIO` (`gServicePump`), legacy APR socket pipes |
| Name caches | `llcachename.*` `llavatarname*.*` `llexperiencecache.*` | 8 | 4,351 | Legacy UUID→name over UDP, display names and experiences over caps |
| Proxy | `llproxy.*` | 2 | 911 | `LLProxy` (HTTP and SOCKS5) |
| Wire data types | `llnamevalue.*` `llpartdata.*` `partsyspacket.*` `patch_*.*` `llinstantmessage.*` `sound_ids.*` `lluseroperation.*` `llclassifiedflags.cpp` `*flags.h` `llregionhandle.h` `lltaskname.h` `llvehicleparams.h` `llfollowcamparams.h` `lldbstrings.h` `llextendedstatus.h` `llinvite.h` `mean_collision_data.h` | 32 | 8,328 | Terrain patch codec, particle data, name-value pairs, flag constants |
| **Total** | | **161** | **53,936** | |

Largest single files: `message.cpp` 3,986; `llassetstorage.cpp` 1,486;
`llcircuit.cpp` 1,441; `message_prehash.cpp` 1,419; `lltransfermanager.cpp`
1,401; `llcorehttputil.cpp` 1,376; `llxfermanager.cpp` 1,328
(`wc -l`).

Tests: `llmessage/tests/` has 18 source/script files, 5,306 lines
(`find llmessage/tests -type f ! -path './fixtures/*' | xargs wc -l`), plus
the 10,235,825-byte corpus `llmessage/tests/fixtures/llmessage_corpus.vksm`.

The message template itself is `scripts/messages/message_template.msg`
(9,296 lines). `grep -cE '^\s*\w+\s+(High|Medium|Low|Fixed)\s+'` finds 483
message headers: High 32, Medium 17, Low 431, Fixed 3; 265 `NotTrusted`,
218 `Trusted`; 189 `Zerocoded`; 17 `UDPDeprecated`, 4 `UDPBlackListed`.
`doc/rust_port.md` reports the parser reading 478 (see open questions).

### `llcorehttp/` (29 files, 8,066 lines)

| Group | Files | Lines | Notes |
|---|---:|---:|---|
| HTTP API over Rust (`httprequest.*` `httpoptions.*` `httpheaders.*` `httpresponse.*` `httphandler.h` `httpcommon.*` `httpstats.*` `bufferarray.*` `bufferstream.*` `_refcounted.*` `_httpservice.h` `_httpinternal.h` `_mutex.h` `_thread.h` `llhttpconstants.*`) | 25 | 5,510 | `httprequest.cpp` 697 lines is the C ABI client; the C++ service is gone |
| WebSocket (`llwebsocketmgr.*` `lljsonrpcws.*`) | 4 | 2,556 | LL 2025 addition on websocketpp, used by the script editor (`newview/llscripteditorws.cpp`) |

Tests and examples: 17 files, 9,158 lines (`find tests examples -type f !
-path 'tests/fixtures/*' | xargs wc -l`), plus
`tests/fixtures/llcorehttp_parity.xml` (274,064 bytes).

### Viewer glue (`newview/`, `viewer_components/login/`)

| Group | Files | Lines | Main subareas |
|---|---|---:|---|
| Handlers | `llviewermessage.{cpp,h}` | 9,252 | 60 `process_*` functions (`grep -cE '^void process_\w+\s*\(' newview/llviewermessage.cpp`), teleport, IM, chat, object/kill dispatch |
| Region and capabilities | `llviewerregion.{cpp,h}` `llcapabilityprovider.h` | 4,931 | Seed request, capability table, region handshake, object cache hooks, interest list |
| Event poll | `lleventpoll.{cpp,h}` | 510 | `EventQueueGet` long-poll coroutine |
| AIS | `llaisapi.{cpp,h}` | 2,031 | AISv3 commands and `AISUpdate` parser |
| Login | `lllogininstance.*` `llxmlrpctransaction.*` `llxmlrpclistener.*` `llloginhandler.*`; `viewer_components/login/lllogin.*` | 2,040 + 507 | XML-RPC login transaction, event-pump driven login coroutine |
| World | `llworld.{cpp,h}` | 2,311 | Region list, circuit setup for neighbours, `EnableSimulator`/`DisableSimulator`/`RegionHandshake` |
| HTTP policy | `llappcorehttp.{cpp,h}` | 881 | 12 policy classes, SSL verification |
| Generic messages | `llviewergenericmessage.{cpp,h}` | 178 | `GenericMessage`, `LargeGenericMessage`, `GenericStreamingMessage` |
| Grid | `llviewernetwork.{cpp,h}` | 972 | Grid manager (login URIs, Second Life vs OpenSim) |
| Startup | `llstartup.{cpp,h}` | 5,500 | Login state machine, `register_viewer_callbacks` |
| **Total** | | **29,113** | (only part of `llstartup.cpp` is networking) |

## Components

### C1. `LLMessageSystem`: the UDP message pump (LL upstream)

- **Responsibility.** One process-wide object (`gMessageSystem`,
  `llmessage/message.h:927`, an `LLPounceable<LLMessageSystem*>`) owning the
  UDP socket, the template map, the circuit table, the packet ring, the
  current builder and reader, and the per-message handler slots.
  Created by `start_messaging_system` (`llmessage/message.cpp:2455`), called
  from `newview/llstartup.cpp:924` after `message_template.msg` is located
  (`llstartup.cpp:879-901`).
- **Key members** (`message.h`): `mPacketRing` (`:295`),
  `mMessageTemplates` (`:307`), `mbProtected` (`:317`), `mSocket` (`:323`),
  `mCircuitInfo` (`LLCircuit`, `:352`), `mMessageBuilder` (`:904`),
  `mTemplateMessageReader` (`:908`), `mLLSDMessageReader` (`:909`).
- **Receive path.** `LLAppViewer::idleNetwork`
  (`newview/llappviewer.cpp:6521`) takes a `LockMessageChecker` and loops
  `checkAllMessages(frame_count, gServicePump)` (`:6542`;
  `message.cpp:3913`), which runs `checkMessages` (`message.cpp:492`) and
  then pumps `gServicePump` (`LLPumpIO`). The loop stops after
  `MESSAGE_MAX_PER_FRAME` = 400 messages (`message.h:66`) or
  `CheckMessagesMaxTime`; backlog is drained into the ring with
  `drainUdpSocket` (`llappviewer.cpp:6575`). Per packet: `LLPacketRing`
  receive (`llpacketring.cpp:71`, SOCKS5 header handling at `:81-93`),
  `zeroCodeExpand` (`message.cpp:2788`, Rust), circuit lookup and
  `checkPacketInID` (`llcircuit.cpp:658`), ack bookkeeping,
  `LLTemplateMessageReader::validateMessage`/`readMessage`
  (`lltemplatemessagereader.cpp:578`, `:615`, Rust decode), trust and ban
  checks, then the handler.
- **Send path.** `newMessageFast` (`message.cpp:3327`) →
  `LLTemplateMessageBuilder` (Rust) → `sendMessage(host)`
  (`message.cpp:1115`) which builds, finds or (when unprotected) creates the
  circuit, refuses dead circuits, and hands the bytes to
  `LLPacketRing::sendPacket` (`llpacketring.cpp:104`) → `send_packet`
  (`net.cpp:336`). Reliable sends (`sendReliable`, `message.cpp:986`,
  `:1013`) register an `LLReliablePacket` with the circuit
  (`llcircuit.cpp:571`).
- **LLSD sends over HTTP.** Messages whose `message.xml` flavor is `llsd` and
  which go to a simulator are posted to the region's
  `UntrustedSimulatorMessage` capability by a coroutine
  (`message.cpp:1184`, `:1381`, body at `:3944`) through
  `HttpCoroutineAdapter`.
- **Per-frame maintenance.** `processAcks` (`message.cpp:766`) runs
  `gTransferManager.updateTransfers()`, `gXferManager->retransmitUnackedPackets()`,
  `gAssetStorage->checkForTimeouts()`, then circuit watchdogs
  (`llcircuit.cpp:801`), resends (`:591`), acks (`:1110`) and queued
  `DenyTrustedCircuit`s. `idleNetwork` calls `retransmitUnackedPackets` and
  `checkForTimeouts` a second time (`llappviewer.cpp:6599`, `:6601`).
- **Handler slots.** One function pointer and one `void**` per template
  (`llmessagetemplate.h:382-400`, `:457`); `setHandlerFuncFast`
  (`message.cpp:2849`) overwrites, so there is exactly one handler per
  message name. `callHandler` (`message.cpp:2862`) applies the trust ban
  and calls it. FS added an exception guard around the call
  (`llmessagetemplate.h:394`, `<FS:ND>`).
- **Ownership/lifetime.** `gMessageSystem` lives from startup to
  `end_messaging_system`; templates are owned by the map; circuits by
  `LLCircuit` (`std::map<LLHost, LLCircuitData*>`, `llcircuit.h:319-340`).
- **Threading.** Main thread only. Handlers run inline in `idleNetwork`.
- **3p.** APR (`apr_socket_t` in `LLMessagePollInfo`, `message.cpp:100`),
  Winsock 2 / BSD sockets (`net.cpp:183-336`).
- **Constants that matter for parity.** Reliable timeout factor 5× averaged
  ping, lost factor 16×, `LL_MAX_LOST_TIMEOUT` 5 s (`message.h:144-151`);
  `LL_MAX_RESENT_PACKETS_PER_FRAME` 100, `LL_MAX_ACKED_PACKETS_PER_FRAME`
  200, `LL_COLLECT_ACK_TIME_MAX` 2 s (`llcircuit.h:61-63`).
- **Firestorm additions visible:** ping ETW tracing (`llcircuit.cpp:973`,
  `:1289`), log-spam throttles (`llcircuit.cpp:127`, `:759`), invalid packet
  exceptions (`message.cpp:690`), OpenSim HTTP options switch
  (`message.cpp:209`, `:3952`), Tracy tags in the reader
  (`lltemplatemessagereader.cpp:491`, `:524`).

### C2. Circuits, reliability, trust (LL upstream)

- `LLCircuitData` (`llcircuit.h:77`) per remote host: in/out packet ids,
  `mUnackedPackets` and `mFinalRetryPackets` (`llcircuit.h:243-247`), ping
  timing (`pingTimerStart`, `llcircuit.cpp:1321`), alive/blocked state
  (`setAlive`, `:497`), trust (`setTrusted`, `:1433`).
- `LLCircuit` (`llcircuit.h:292`): the table, the unacked and send-ack maps,
  `resendUnackedPackets`, `sendAcks`, `updateWatchDogTimers`.
- Circuit establishment for the viewer: `enableCircuit(host, trusted)`
  (`message.cpp:1535`) then `UseCircuitCode` (reliable). Login
  (`llstartup.cpp:2489-2518`, retries from `UseCircuitCodeMaxRetries` and
  `UseCircuitCodeTimeout`), neighbours (`newview/llworld.cpp:1584`
  `process_enable_simulator`, `:1639` `enableCircuit`, `:1647`
  `UseCircuitCode`), teleport (`llviewermessage.cpp:3686` `enableCircuit`,
  `:3725` `UseCircuitCode`).
- Trusted circuits (`CreateTrustedCircuit`, `DenyTrustedCircuit`,
  `sendCreateTrustedCircuit` `message.cpp:3141`) and
  `LLTrustedMessageService` (`/trusted-message/<name>`,
  `message.cpp:3984`) are server-to-server facilities; the viewer only
  registers the handlers (`message.cpp:2520-2534`). Inference: they are
  dead weight in the viewer apart from the registration.

### C3. Template codec and data packers (LL upstream classes, Rust bodies)

- Classes: `LLTemplateTokenizer`/`LLTemplateParser`
  (`llmessagetemplateparser.cpp`), `LLMessageTemplate` with its compiled
  handle (`llmessagetemplate.cpp:43`), `LLTemplateMessageBuilder`
  (`lltemplatemessagebuilder.cpp:75` `newMessage`, `:113` `nextBlock`,
  `:169` `addData`, `:360` `buildMessage`), `LLTemplateMessageReader`
  (`lltemplatemessagereader.cpp:83` `getData`, `:485` `decodeData`, `:649`
  `copyToBuilder`), `LLDataPacker*` (`lldatapacker.cpp`).
- `LLSDMessageBuilder`/`LLSDMessageReader` stay C++ and are what event-queue
  messages are read through (see C6).
- Name keys: `LLMessageStringTable` (`message.h:68`) canonical pointers;
  `message_prehash.cpp` (1,419 lines) defines the `_PREHASH_*` constants.
  FS adds names (`message_prehash.h` has 5 `<FS:` markers).
- Details of the Rust boundary are in `doc/rust_port.md`, rung three, and in
  "Rust port status" below.

### C4. Xfer (`LLXferManager`, LL upstream)

- `gXferManager` (`llxfermanager.h:209`), handlers `ConfirmXferPacket`,
  `RequestXfer`, `SendXferPacket`, `AbortXfer` (`llxfermanager.cpp:350-353`),
  registered from `llstartup.cpp:2319`; ack throttling configured at
  `llstartup.cpp:997-1002`.
- Viewer uses (`grep -rn 'gXferManager->' newview --include=*.cpp`): task
  inventory file download after `ReplyTaskInventory`
  (`newview/llviewerobject.cpp:3538`, aborts at `:3166`, `:3550`), mute list
  (`llmutelist.cpp:990`), `InitiateDownload` (`llviewermessage.cpp:8570-8575`),
  estate/terrain uploads and downloads (`llfloaterregioninfo.cpp:2279`,
  `:2298`, `llfloatergodtools.cpp:1345`).
- Threading: main thread; file I/O through `LLFileSystem`/`LLFile`.

### C5. Transfer manager (`LLTransferManager`, LL upstream)

- `gTransferManager` (`lltransfermanager.h:497`), initialised at the end of
  `start_messaging_system` (`message.cpp:2537`), handlers
  `TransferRequest`, `TransferInfo`, `TransferPacket`, `TransferAbort`
  (`lltransfermanager.cpp:84-87`), updated from `processAcks`
  (`message.cpp:770`).
- Viewer use is the UDP asset path: `LLAssetStorage` estate and
  inventory-item assets (`llassetstorage.cpp:767-778`, `:905-921`) and
  `LLViewerAssetStorage` falling back to UDP when there is no `ViewerAsset`
  capability and the grid is not Second Life
  (`newview/llviewerassetstorage.cpp:558-585`, `<FS:Ansariel> [UDP Assets]`).
  Inference: on Second Life this path is inactive; it serves OpenSim.

### C6. Event queue and the `LLHTTPNode` dispatch tree

- **Event poll** (`newview/lleventpoll.cpp`): `LLViewerRegion::setCapability`
  creates an `LLEventPoll` when `EventQueueGet` arrives
  (`llviewerregion.cpp:3644-3648`), replacing any previous one. The impl
  (`lleventpoll.cpp:52`) uses policy `AP_LONG_POLL` (`:99`) and a coroutine
  (`eventPollCoro`, `:158`, launched at `:135`) that posts `{ack, done}`
  (`:203-212`) with `postAndSuspend`, treats a libcurl timeout as "no
  events" (`:230`), backs off on errors (1 s + 3 s per error, at most 15,
  `:78-83`), and for each event posts the body to the main work queue
  (`:380-404`) where `handleMessage` calls `LLMessageSystem::dispatch`
  (`:110`, `:127`).
- Firestorm additions: 60 s transfer timeout and pre-coroutine retry
  behaviour (`<FS:Ansariel>`, `:181-188`, `:261-271`), the
  `FSEventPollCoreRetries` setting for FIRE-36454 (`<FS:Beq>`, `:165-180`,
  `:240-247`, `:278-285`, `:311-316`), a guard for messages without a body
  (`<FS:ND>`, `:119-125`).
- **Dispatch** (`message.cpp:2051-2092`): checks the name against the
  template map or `LLMessageConfig`, traverses `/message/<name>` in the
  `LLHTTPNode` tree and posts. Two kinds of node answer:
  - the wildcard `LLMessageHandlerBridge` (`message.cpp:104-137`), which
    sets `mLastSender`, loads the body into `mLLSDMessageReader` and calls
    the **same template handler slot** a UDP message would reach. So
    `EnableSimulator`, `TeleportFinish`, `TeleportFailed`, `CrossedRegion`,
    `ParcelProperties`, `ScriptRunningReply` etc. delivered as LLSD run the
    handler registered in `register_viewer_callbacks`, reading through the
    `LLSDMessageReader`;
  - named `LLHTTPRegistration<…>` nodes (19 in `newview`, listed under
    Coupling) for messages that exist only as LLSD (ChatterBox sessions,
    display names, `EstablishAgentCommunication`, voice, pathfinding).
- `message.xml` (`newview/app_settings/message.xml`, 768 lines) declares 68
  message entries: 40 `llsd` flavor, 28 `template` flavor, plus `capBans`
  (`:648`), `messageBans` (`:754`) and `maxQueuedEvents` (`:765`).
- Threading: the coroutine runs on the main thread's fiber scheduler; the
  HTTP request itself runs on the Rust HTTP service thread.

### C7. HTTP: `llcorehttp` (C++ API over Rust) and `LLAppCoreHttp`

- Public API unchanged: `LLCore::HttpRequest` (`llcorehttp/httprequest.cpp`:
  `requestGet` `:443`, `requestGetByteRange` `:452`, `requestPost` `:463`,
  `requestPut` `:473`, `requestDelete`/`Patch`/`Copy`/`Move` `:483-511`,
  `deliver` `:531`, `update` `:595`, `requestCancel` `:625`,
  `createService` `:638`, `startThread` `:673`), with the host hook table
  built by `make_hooks` (`:204`).
- `LLAppCoreHttp` (`newview/llappcorehttp.cpp:156` `init`, `:341`
  `requestStop`, `:353` `cleanup`, `:402` `refreshSettings`, `:536`
  `sslVerify`) creates the service at `llappviewer.cpp:1006` and defines 12
  policy classes (`llappcorehttp.h:62-204`): `AP_DEFAULT`, `AP_ASSET`,
  `AP_TEXTURE`, `AP_MESH1`, `AP_MESH2`, `AP_LARGE_MESH`, `AP_UPLOADS`,
  `AP_LONG_POLL`, `AP_INVENTORY` (= `AP_REPORTING`), `AP_MATERIALS`,
  `AP_AGENT`, with concurrency from settings (`init_data`, `:68`).
- Reply delivery: each `HttpRequest` instance has its own reply queue and is
  drained by whoever owns it calling `update()` — the coroutine pumper on
  the `"mainloop"` event pump (`llmessage/llcorehttputil.cpp:637-653`),
  `LLAppCoreHttp` itself (`llappcorehttp.cpp:372`), the inventory model
  (`llinventorymodel.cpp:3607`, foreground and background instances), the
  mesh repository thread (`llmeshrepository.cpp:3212-3266`), the media data
  client (`llmediadataclient.cpp:384`), and the texture fetch thread.
- Threading: the service thread is Rust (`vkstorm-http`); callers live on
  the main thread (coroutines, inventory), the texture fetch thread and the
  mesh repository thread, each with its own `HttpRequest`.
- 3p: libcurl 7.54.1 with OpenSSL 1.1.1w and nghttp2 (`llcorehttp/CMakeLists.txt:66-74`
  links `ll::libcurl`, `ll::openssl`, `ll::nghttp2`, `ll::websocketpp`,
  `vkstorm_ffi`). `llcorehttp` includes `llmessage` headers privately
  (`CMakeLists.txt:79`) while `llmessage` links `llcorehttp`: a circular
  include dependency noted in the CMake comment.

### C8. Coroutine HTTP helpers (`llcorehttputil`, `LLCoprocedureManager`)

- `LLCoreHttpUtil::HttpCoroutineAdapter` (`llcorehttputil.h:307`) wraps one
  request in an `LLEventStream` reply pump and suspends the calling fiber
  (`llcorehttputil.cpp:688-896`, `llcoro::suspendUntilEventOn`). Variants
  `get/post/put/delete/patch/copy/move…AndSuspend`, raw, file and JSON
  bodies (`llcorehttputil.h:331-450`). Results are an LLSD map with
  `HTTP_RESULTS` keys (`:310-318`). Handlers: `HttpCoroHandler`
  (`llcorehttputil.cpp:252`), LLSD (`:389`), raw (`:497`), JSON (`:572`).
- Free functions `requestPostWithLLSD` etc. (`llcorehttputil.h:117-146`)
  for callers using `HttpHandler` callbacks rather than coroutines.
- `FSCoreHttpUtil` (`fscorehttputil.{h,cpp}`, Firestorm, 2015) adds
  callback-style helpers; used in 6 `newview` files.
- `LLCoprocedureManager` (`llcoproceduremanager.cpp`): named pools of
  coroutines fed by a `boost::fibers::buffered_channel` (`:120`), default
  sizes `Upload` 1, `AIS` 1 (`:45-49`, the comment says AIS is serialised to
  keep the COF in sync), others 5; `ExpCache` pool in
  `llexperiencecache.cpp:119`; asset storage pool in
  `newview/llviewerassetstorage.cpp:121`.
- Usage (excluding `tests/`): 176 `…AndSuspend(` call sites in 67 files
  (`newview`, `llmessage`, `viewer_components`); `HttpCoroutineAdapter` in
  92 `.cpp` files; 139 `LLCoros::instance().launch(` sites in 64 `newview`
  files; 9 `enqueueCoprocedure(` sites in `newview`.
- Coroutines are `boost::fibers` (`llcommon/llcoros.h:33-35`), scheduled on
  the main thread.

### C9. Capabilities and region setup (`LLViewerRegion`, `LLWorld`)

- `LLWorld::addRegion` (`newview/llworld.cpp:520`) creates the
  `LLViewerRegion`; `removeRegion` (`:677`), `disconnectRegions` (`:1560`).
  `LLWorld` owns the region list; each `LLViewerRegion` owns an
  `LLViewerRegionImpl` (`llviewerregion.cpp:183`) holding the host, the
  capability map and the raw `LLEventPoll*`.
- Seed: `setSeedCapability` (`llviewerregion.cpp:3597`) stores `Seed` and
  launches `requestBaseCapabilitiesCoro` (`:259`, launched at `:3626`),
  which posts the name list from `buildCapabilityNames` (`:3457`) to the
  seed URL with `AP_DEFAULT`, retries up to `mSeedCapMaxAttempts`
  (`MAX_CAP_REQUEST_ATTEMPTS` = 30, `:106`), stores each returned URL via
  `setCapability` (`:387`) and marks `setCapabilitiesReceived(true)`
  (`:399`, `:3775`). `requestBaseCapabilitiesCompleteCoro` (`:413`) is the
  second pass used when the seed changes.
- `setCapability` (`:3642`) special-cases `EventQueueGet` (event poll),
  `UntrustedSimulatorMessage` (stored on the `LLHost`), `SimulatorFeatures`
  (fetch features, `:2565`), `ViewerAsset` (and a `VIEWERASSET`
  environment override, a MAINT-7081 test hack) and `GetTexture`
  (`<FS:Ansariel> [UDP Assets]`).
- `getCapability`/`isCapabilityAvailable` (`:3733`, `:3749`) warn when
  called before caps arrive, except for `Seed` and `ObjectMedia`.
- Region handshake: `process_region_handshake` (`llworld.cpp:1728`) →
  `LLViewerRegion::unpackRegionHandshake` (`llviewerregion.cpp:3241`) →
  `RegionHandshakeReply` (`:3433`).
- Seed sources: login response `seed_capability` (`llstartup.cpp:4963`,
  applied at `:2145`), `EstablishAgentCommunication` for neighbours
  (`llworld.cpp:1973` node, `setSeedCapability` at `:1709`),
  `TeleportFinish`'s `SeedCapability` field (`llviewermessage.cpp:3721`,
  applied at `:3739`), `CrossedRegion`.
- Interest list: `InterestList` capability posted and deleted through
  `requestPostCapability`/`requestDelCapability`
  (`llviewerregion.cpp:3861-3903`).

### C10. Login (`LLLoginInstance`, `LLLogin`, `LLXMLRPCTransaction`)

- `LLLoginInstance` (`newview/lllogininstance.cpp`) builds the parameters
  (`constructAuthParams`, `:155`): 23 `requested_options` for Second Life
  (inventory root and skeleton, library root/owner/skeleton, initial
  outfit, gestures, display names, buddy list, configs, `map-server-url`,
  `voice-config`, `login-flags`, `global-textures`, …) and 5 more under
  `OPENSIM` (`<FS:AW>`: `currency`, `max_groups`, `search`,
  `destination_guide_url`, `avatar_picker_url`), plus the request fields
  (`:225-241`). Responses are dispatched by `LLEventDispatcher` on
  `fail.login`, `connect`, `disconnect`, `indeterminate` (`:89-98`).
- `LLLogin` (`viewer_components/login/lllogin.cpp:135` `loginCoro`) runs a
  coroutine that posts to the `"LLXMLRPCTransaction"` event pump and waits
  on the reply pump (`:144`, `:174`).
- `LLXMLRPCListener` (`newview/llxmlrpclistener.cpp`) answers that pump by
  creating an `LLXMLRPCTransaction` (`newview/llxmlrpctransaction.cpp`),
  which posts the XML-RPC body with `HttpRequest::requestPost` on
  `DEFAULT_POLICY_ID` (`:258`) and parses the reply with `LLXMLNode` into
  LLSD (`parseValue`, `:308`); polled from `process()` (`:313`, `:367`).
  Inference: there is no xmlrpc-epi dependency left in this tree; login is
  XML-RPC only, not an LLSD login.
- Startup consumes `circuit_code`, `sim_ip`, `sim_port`, `seed_capability`
  (`llstartup.cpp:4917-4963`) and later the inventory and buddy-list
  options (`:2654-2726`, `:5025`, `:5146`).
- Startup states (`llstartup.h:58-92`) relevant here: `STATE_LOGIN_AUTH_INIT`,
  `STATE_LOGIN_PROCESS_RESPONSE`, `STATE_WORLD_INIT` (`llstartup.cpp:2050`,
  first region at `:2136`), `STATE_SEED_CAP_GRANTED` (`:2269`; registers the
  handlers at `:2306`, xfer at `:2319`, `UseCircuitCode` at `:2489-2518`),
  `STATE_AGENT_SEND` (`:2529`; `AgentMovementComplete` handler at `:2537`),
  `STATE_INVENTORY_SEND` (`:2625`; inventory callbacks at `:2886`).

### C11. Teleport and region crossing

- Request: `LLAgent::teleportRequest` (`newview/llagent.cpp:5093`),
  `TeleportLandmarkRequest` (`:5178`), `TeleportLureRequest` (`:5216`),
  `TeleportLocationRequest` (`:5385`), state in `setTeleportState`
  (`:5537`), restartable requests (`LLTeleportRequest*`, `:6132-6324`).
- Server answers: `TeleportStart` (`llviewermessage.cpp:3455`),
  `TeleportProgress` (`:3506`), `TeleportFinish` (`:3641`, by event queue),
  `TeleportLocal` (`:7826`), `TeleportFailed` (`:7731`). `TeleportFinish`
  enables the circuit, adds the region, sets it as the agent region, sends
  `UseCircuitCode` and `CompleteAgentMovement` (`send_complete_agent_movement`,
  `:500`) and sets the seed; `AgentMovementComplete` (`:3842`) completes.
  `CrossedRegion` (`:4109`) is the walking equivalent.
- Firestorm/RLVa: `llviewermessage.cpp` has 133 `<FS:` markers and 182
  case-insensitive `rlv` matches; `llagent.cpp` 112 and 86
  (`grep -cE '<FS:'`, `grep -ci rlv`). Aurora/OpenSim variable region size
  paths are marked `<FS:CR> Aurora Sim` (`llviewermessage.cpp:3700`,
  `llworld.cpp:1620`, `llviewerregion.cpp:3479`).

### C12. AISv3 client (`AISAPI`, `AISUpdate`)

- Capabilities `InventoryAPIv3` and `LibraryAPIv3` (`newview/llaisapi.cpp:50-51`),
  requested through `AISAPI::getCapNames` (`:77`) inside the
  `use_http_inventory()` block of `buildCapabilityNames`
  (`llviewerregion.cpp:3486-3497`, `<FS:Ansariel> Force HTTP features on SL`).
- Commands (`llaisapi.cpp:104-758`): `CreateInventory`, `SlamFolder`,
  `RemoveCategory`, `RemoveItem`, `CopyLibraryCategory`,
  `PurgeDescendents`, `UpdateCategory`, `UpdateItem`, `FetchItem`,
  `FetchCategoryChildren` (two overloads), `FetchCategoryCategories`,
  `FetchCategorySubset`, `FetchCOF`, `FetchCategoryLinks`, `FetchOrphans`.
  Each binds an `HttpCoroutineAdapter` member function and is queued by
  `EnqueueAISCommand` (`:791`) into the `AIS` coprocedure pool (size 1),
  with an overflow list drained by `onIdle` (`:818`); the coroutine body is
  `InvokeAISCommandCoro` (`:856`).
- `AISUpdate` (`:1081-1643`) parses the HAL+JSON-as-LLSD response
  (`parseMeta`, `parseContent`, `parseItem`, `parseLink`, `parseCategory`,
  `parseEmbedded*`) and applies it to `gInventory` in `doUpdate` (`:1643`),
  with a frame-time budget (`checkTimeout`, `:1096`).
- Firestorm: 4 `<FS:` markers in `llaisapi.cpp`.

### C13. Legacy name caches and other UDP helpers in `llmessage`

- `LLCacheName` (`llcachename.cpp:273-280`: `UUIDNameRequest`/`Reply`,
  `UUIDGroupNameRequest`/`Reply`; 19 `<FS:` markers), `LLAvatarNameCache`
  (display names over the `GetDisplayNames` capability; referenced in 115
  `newview` files), `LLExperienceCache` (capabilities through the `ExpCache`
  coprocedure pool).
- `LLDispatcher` (`lldispatcher.cpp`): sub-dispatch by method string for
  `GenericMessage` (`gGenericDispatcher`, 10 `addHandler` sites in
  `newview`, including inventory's `BulkUpdateInventory` method at
  `llinventorymodel.cpp:3599`) and `EstateOwnerMessage`
  (`llfloaterregioninfo.cpp:324`, `:2374-2382`).
- `GenericStreamingMessage` carries GLTF material overrides
  (`newview/llviewergenericmessage.cpp:96-108`).
- `LLPumpIO`/`LLIOPipe` survive for `gServicePump`, used by the voice
  clients (`llvoiceclient`, `llvoicevivox`, `llvoicewebrtc`);
  `LLIOHTTPServer`, `LLIOSocket`, `LLSDHTTPServer`, `LLServiceBuilder` and
  `LLTrustedMessageService` have no `newview` references (`grep -rlw`).

## Coupling

### Include sites

Command (from `indra/`, `.cpp`/`.h`/`.hpp`/`.inl`, all directories):

```sh
grep -rlE "#\s*include\s*[\"<]$h[\">]" --include=*.cpp --include=*.h --include=*.hpp --include=*.inl . | wc -l
```

| Header | Files | Notes |
|---|---:|---|
| `message.h` | 144 | newview 92, llmessage 31, llinventory 9, llprimitive 4, test 3, llui 3, llcharacter 2 |
| `message_prehash.h` | 8 | mostly reached through `message.h` |
| `llhost.h` | 37 | |
| `llcircuit.h` | 2 | circuits are private to `message.cpp` |
| `llpacketring.h` | 2 | |
| `llxfermanager.h` | 15 | |
| `lltransfermanager.h` | 8 | |
| `llassetstorage.h` | 38 | |
| `llcachename.h` | 27 | |
| `llhttpnode.h` | 21 | |
| `lldispatcher.h` | 17 | |
| `llcorehttputil.h` | 84 | newview 76, llmessage 7, llcrashlogger 1 |
| `fscorehttputil.h` | 7 | Firestorm |
| `llcoproceduremanager.h` | 10 | |
| `httprequest.h` | 21 | |
| `httpcommon.h` | 26 | |
| `httpheaders.h` | 23 | |
| `httpoptions.h` | 17 | |
| `bufferarray.h` | 19 | |
| `llproxy.h` | 10 | |
| `llviewerregion.h` | 233 | all newview |
| `llworld.h` | 110 | |
| `llviewernetwork.h` | 119 | grid manager |
| `llviewermessage.h` | 43 | |
| `lllogininstance.h` | 18 | |
| `llviewergenericmessage.h` | 16 | |
| `llaisapi.h` | 9 | |
| `llxmlrpctransaction.h` | 4 | |
| `llappcorehttp.h` | 3 | |
| `lleventpoll.h` | 2 | |

`message.h` and `llviewerregion.h` are the two wide couplings: almost every
viewer feature that sends a message includes `message.h`, and anything that
asks for a capability or region state includes `llviewerregion.h`.

Outgoing UDP: 323 `newMessage(`/`newMessageFast(` sites in 92 `newview`
`.cpp` files, 191 distinct message names
(`grep -rhoE --exclude-dir=tests 'newMessage(Fast)?\s*\(' --include=*.cpp newview`).
Top senders: `llagent.cpp` 23, `llviewermessage.cpp` 19,
`llviewerinventory.cpp` 17, `llviewerparcelmgr.cpp` 16, `llgroupmgr.cpp` 13,
`llviewerobject.cpp` 11 (counts include tests directories for these
per-file figures; none of these files is under `tests/`).

### Message handler registry

`grep -rcE 'setHandlerFunc(Fast)?\s*\(' --include=*.cpp --include=*.h .`
finds 109 `setHandlerFuncFast(` and 82 `setHandlerFunc(` occurrences in
`.cpp` files (including comments, the definitions and tests). Counting only
active registrations of the running viewer:

| Where | Active | Messages |
|---|---:|---|
| `llmessage/message.cpp:2509-2534` (`start_messaging_system`) | 11 | `StartPingCheck`, `CompletePingCheck`, `OpenCircuit`, `CloseCircuit`, `AddCircuitCode`, `UseCircuitCode`, `PacketAck`, `CreateTrustedCircuit`, `DenyTrustedCircuit`, `Error`, `RequestTrustedCircuit` (3 more commented out) |
| `llmessage/llxfermanager.cpp:350-353` | 4 | xfer |
| `llmessage/lltransfermanager.cpp:84-87` | 4 | transfer |
| `llmessage/llcachename.cpp:273-280` | 4 | UUID name/group name request and reply |
| `llmessage/llassetstorage.cpp:358` | 1 | `AssetUploadComplete` (cleared at `:374`) |
| `newview/llstartup.cpp:3812-4005` (`register_viewer_callbacks`) | 111 | see next table (5 more commented out) |
| `newview/llstartup.cpp:3071-3074` | 4 | `SoundTrigger`, `PreloadSound`, `AttachedSound`, `AttachedSoundGainChange` (null handlers at `:2404-2405` when audio is off) |
| `newview/llstartup.cpp:2537` | 1 | `AgentMovementComplete` |
| `newview/llinventorymodel.cpp:3902-3924` (`registerCallbacks`) | 9 | `UpdateCreateInventoryItem`, `RemoveInventoryItem`, `RemoveInventoryFolder`, `RemoveInventoryObjects`, `SaveAssetIntoInventory`, `BulkUpdateInventory`, `MoveInventoryItem`, `InventoryDescendents`, `FetchInventoryReply` (2 commented out) |
| `llinventory/lllandmark.cpp:210` | 1 | `RegionIDAndHandleReply` |
| `newview/llcallingcard.cpp:667-675` | 5 | `FindAgent`, `OnlineNotification`, `OfflineNotification`, `TerminateFriendship`, `ChangeUserRights` |
| `newview/llviewerparcelmedia.cpp:82-83` | 2 | `ParcelMediaCommandMessage`, `ParcelMediaUpdate` |
| `newview/llfloatertelehub.cpp:58` | 1 | `TelehubInfo` (while the floater is open; cleared at `:88`) |
| `newview/llfloaterpay.cpp:408` | 1 | `PayPriceReply` (per floater; cleared at `:159`, `:373`) |
| `newview/llfloaterregioninfo.cpp:324` | 1 | `EstateOwnerMessage` (then `LLDispatcher`) |
| **Total** | **160** | |

`register_viewer_callbacks` by subsystem (the 111 names extracted with
`sed -n 3812,4005p newview/llstartup.cpp | grep -vE '^\s*//' | grep -oE 'setHandlerFunc(Fast)?\s*\(\s*(_PREHASH_\w+|"\w+")'`;
grouping is this survey's):

| Subsystem | Count | Messages |
|---|---:|---|
| Region, circuit, session, teleport | 17 | `EnableSimulator`, `DisableSimulator`, `CrossedRegion`, `TeleportStart`, `TeleportProgress`, `TeleportFinish`, `TeleportFailed`, `TeleportLocal`, `RegionHandshake`, `RegionInfo`, `SimulatorViewerTimeMessage`, `SimStats`, `HealthMessage`, `EconomyData`, `KickUser`, `LogoutReply`, `FeatureDisabled` |
| Objects (scene) | 14 | `ObjectUpdate`, `ObjectUpdateCompressed`, `ObjectUpdateCached`, `ImprovedTerseObjectUpdate`, `KillObject`, `ObjectProperties`, `ObjectPropertiesFamily`, `ForceObjectSelect`, `NameValuePair`, `RemoveNameValuePair`, `ObjectAnimation`, `DerezContainer`, `DeRezAck`, `ViewerEffect` |
| Avatars, agent, appearance | 14 | `AvatarAnimation`, `AvatarAppearance`, `AgentCachedTextureResponse`, `RebakeAvatarTextures`, `AgentWearablesUpdate`, `AvatarSitResponse`, `CameraConstraint`, `SetFollowCamProperties`, `ClearFollowCamProperties`, `AgentDataUpdate`, `AgentGroupDataUpdate`, `AgentDropGroup`, `CoarseLocationUpdate`, `GrantGodlikePowers` |
| Groups | 13 | `CreateGroupReply`, `JoinGroupReply`, `EjectGroupMemberReply`, `LeaveGroupReply`, `GroupProfileReply`, `GroupMembersReply`, `GroupRoleDataReply`, `GroupRoleMembersReply`, `GroupTitlesReply`, `GroupNoticesListReply`, `GroupAccountSummaryReply`, `GroupAccountDetailsReply`, `GroupAccountTransactionsReply` |
| Profiles, people | 10 | `AvatarPropertiesReply`, `AvatarInterestsReply`, `AvatarGroupsReply`, `AvatarNotesReply`, `AvatarPicksReply`, `AvatarClassifiedReply`, `PickInfoReply`, `ClassifiedInfoReply`, `AvatarPickerReply`, `UserInfoReply` |
| Parcels, land, estate | 9 | `ParcelOverlay`, `ParcelProperties`, `ParcelAccessListReply`, `ParcelDwellReply`, `ParcelInfoReply`, `ParcelObjectOwnersReply`, `LandStatReply`, `EstateCovenantReply`, `PlacesReply` |
| Search, map, events | 9 | `DirPlacesReply`, `DirPeopleReply`, `DirEventsReply`, `DirGroupsReply`, `DirClassifiedReply`, `DirLandReply`, `MapBlockReply`, `MapItemReply`, `EventInfoReply` |
| Scripts, task inventory | 7 | `ReplyTaskInventory`, `ScriptRunningReply`, `ScriptQuestion`, `ScriptControlChange`, `ScriptDialog`, `ScriptTeleportRequest`, `LoadURL` |
| Chat, IM, alerts | 6 | `ChatFromSimulator`, `ImprovedInstantMessage`, `AlertMessage`, `AgentAlertMessage`, `MeanCollisionAlert`, `ViewerFrozenMessage` |
| Terrain, textures | 4 | `LayerData`, `ImageData`, `ImagePacket`, `ImageNotInDatabase` |
| Generic, downloads | 4 | `GenericMessage`, `GenericStreamingMessage`, `LargeGenericMessage`, `InitiateDownload` |
| Calling cards | 3 | `OfferCallingCard`, `AcceptCallingCard`, `DeclineCallingCard` |
| Money | 1 | `MoneyBalanceReply` |

Inventory-relevant handlers therefore sit in three places: the 9 in
`LLInventoryModel::registerCallbacks`, `ReplyTaskInventory` →
`LLViewerObject::processTaskInv` (`llstartup.cpp:3871`), and
`ImprovedInstantMessage` (inventory offers, `process_improved_im`,
`:3857`) plus `GenericMessage`'s `BulkUpdateInventory` method.

### LLSD-only handlers (`LLHTTPRegistration`)

`grep -rnE 'LLHTTPRegistration\s*<' --include=*.cpp newview` gives 19
registrations (path in parentheses):

| File | Messages |
|---|---|
| `llagent.cpp:4298`, `:4421` | `AgentDropGroup`, `AgentGroupDataUpdate` |
| `llimview.cpp:5228-5248` | `ChatterBoxSessionStartReply`, `ChatterBoxSessionEventReply`, `ForceCloseChatterBoxSession`, `ChatterBoxSessionAgentListUpdates`, `ChatterBoxSessionUpdate`, `ChatterBoxInvitation` |
| `llviewerdisplayname.cpp:239`, `:243` | `SetDisplayNameReply`, `DisplayNameUpdate` |
| `llvoiceclient.cpp:1221`, `:1225` | `ParcelVoiceInfo`, `RequiredVoiceVersion` |
| `llpathfindingmanager.cpp:86`, `:98` | `NavMeshStatusUpdate`, `AgentStateUpdate` |
| `llworld.cpp:1973` | `EstablishAgentCommunication` |
| `llviewerregion.cpp:2478` | `CoarseLocationUpdate` (also a UDP handler, `LLWorld::processCoarseUpdate`) |
| `llviewerobject.cpp:8266` | `ObjectPhysicsProperties` |
| `llfloaterregiondebugconsole.cpp:209` | `SimConsoleResponse` |
| `llpanelopenregionsettings.cpp:208` | `OpenRegionInfo` (Firestorm, Aurora/OpenSim) |

`llmessage` adds the wildcard `/message/<message-name>` bridge
(`message.cpp:137`), `/trusted-message/<message-name>` (`:3984`) and 12
service nodes in `llsdappservices.cpp`/`llsdhttpserver.cpp` with no viewer
callers.

### Capabilities

`buildCapabilityNames` (`llviewerregion.cpp:3457-3594`) appends 118 names
(`sed -n 3457,3596p newview/llviewerregion.cpp | grep -c 'capabilityNames.append('`),
plus 2 from `AISAPI::getCapNames`, 120 requested in all. 10 of them (8
fetch/task names and the 2 AIS names) are inside `if (use_http_inventory())`
(`:3486-3497`). Firestorm additions: `DispatchOpenRegionSettings`
(`<FS:CR>`), `GetMesh`/`GetMesh2`/`GetTexture` (`<FS:Ansariel> [UDP
Assets]`), `VETPBR` (OpenSim PBR terrain, `<FS:Beq>`).

Use sites (excluding `tests/`): 139 `getCapability(`/`isCapabilityAvailable(`
lines in 67 `newview` `.cpp` files; 113 of them pass a string literal, naming
59 distinct capabilities. Three literals used are not in the request list:
`Seed` (set locally), `NewAgentInventory` and `FlickrConnect` (Inference:
dead code paths; the region never returns those names because they are never
requested). Other sites pass constants (`AISAPI::INVENTORY_CAP_NAME`) or go
through `requestPostCapability` / `requestGetCapability` /
`requestDelCapability` (11 call sites).

By consumer (literal-use files from `grep -rlE '"<cap>"' newview --include=*.cpp`):

| Family | Capabilities | Consumers |
|---|---|---|
| Agent inventory | `FetchInventory2`, `FetchInventoryDescendents2`, `FetchLib2`, `FetchLibDescendents2`, `InventoryAPIv3`, `LibraryAPIv3`, `CreateInventoryCategory`, `IncrementCOFVersion`, `CopyInventoryFromNotecard`, `InventoryThumbnailUpload`, `NewFileAgentInventory`, `Update{Gesture,Notecard,Settings,Material}AgentInventory`, `UpdateScriptAgent` | `llinventorymodelbackgroundfetch.cpp`, `llviewerinventory.cpp`, `llinventoryobserver.cpp`, `llinventorymodel.cpp`, `llaisapi.cpp`, `llappearancemgr.cpp`, `llviewermenufile.cpp`, `llmeshrepository.cpp`, previews |
| Task inventory | `RequestTaskInventory`, `CreateTaskInventoryItem`, `Update{Gesture,Notecard,Settings,Material}TaskInventory`, `UpdateScriptTask` | `llviewerobject.cpp:3131-3201`, `llpreviewscript.cpp`, `llcompilequeue.cpp`, `llscripteditorws.cpp` |
| Objects, materials | `ObjectMedia`, `ObjectMediaNavigate`, `GetObjectCost`, `GetObjectPhysicsData`, `ResourceCostSelected`, `ModifyMaterialParams`, `RenderMaterials`, `ObjectAnimation`, `InterestList`, `RegionObjects`, `AttachmentResources` | `llmediadataclient.cpp`, `llviewerobjectlist.cpp`, `llmaterialmgr.cpp`, `llgltfmateriallist.cpp`, `llvovolume.cpp`, `llviewerregion.cpp` |
| Assets | `ViewerAsset`, `GetMesh`, `GetMesh2`, `GetTexture`, `UploadBakedTexture`, `MeshUploadFlag` | `llviewerassetstorage.cpp`, `llmeshrepository.cpp`, `lltexturefetch.cpp` |
| Avatars, names | `UpdateAvatarAppearance`, `AvatarRenderInfo`, `AgentProfile`, `UploadAgentProfileImage`, `GetDisplayNames`, `SetDisplayName`, `AvatarPickerSearch` | `llappearancemgr.cpp`, profiles, `llavatarnamecache.cpp` |
| Chat, IM, friends | `ChatSessionRequest`, `ReadOfflineMsgs`, `AcceptFriendship`, `DeclineFriendship`, `AcceptGroupInvite`, `DeclineGroupInvite` | `llimview.cpp`, `fsfloaterim.cpp`, `llimprocessing.cpp`, `llspeakers.cpp`, `llvoicechannel.cpp` |
| Region, parcel, estate | `SimulatorFeatures`, `ParcelPropertiesUpdate`, `RemoteParcelRequest`, `ModifyRegion`, `EstateAccess`, `EstateChangeInfo`, `DispatchRegionInfo`, `LandResources`, `RegionSchedule`, `HomeLocation`, `ExtEnvironment`, `EnvironmentSettings` | parcel manager, region info floaters, environment |
| Groups | `GroupAPIv1`, `GroupMemberData`, `GroupExperiences`, `StartGroupProposal`, `GroupProposalBallot` | `llgroupmgr.cpp` |
| Experiences | 13 `*Experience*` names | `llexperiencecache.cpp` |
| Voice | `ProvisionVoiceAccountRequest`, `VoiceSignalingRequest`, `ParcelVoiceInfoRequest` | voice clients |
| Transport | `EventQueueGet`, `UntrustedSimulatorMessage`, `Seed` | `lleventpoll.cpp`, `message.cpp` |
| Statistics, misc | `ViewerStats`, `ViewerMetrics`, `TextureStats`, `ViewerBenefits`, `SearchStat*`, `SendUserReport*`, `SendPostcard`, `LSLSyntax`, `ServerReleaseNotes`, pathfinding names | various |

## Rust port status

### What is Rust now

| Area | ABI prefix | Call sites (`grep -oE '\bvks_<p>\w+'` distinct / `grep -cE` lines) | C++ wrapper |
|---|---|---|---|
| Template parser, compiled templates, builder, reader, zero-coding | `vks_msg_*` | 51 distinct functions in `llmessage/*.cpp`; lines: `llmessagetemplateparser.cpp` 27, `lltemplatemessagebuilder.cpp` 22, `lltemplatemessagereader.cpp` 14, `llmessagetemplate.cpp` 6, `message.cpp` 2 (`vks_msg_zero_code_gain` `:2783`, `vks_msg_zero_code_expand` `:2815`) | Class names and interfaces unchanged (rung three) |
| Data packers (ASCII buffer/file text, binary strings and sized data, fixed point) | `vks_dp_*` | 12 distinct, 25 lines in `lldatapacker.cpp` | `LLDataPacker*`; fixed-size binary copies stay inline C++ by measurement (`doc/rust_port.md`, rung three) |
| HTTP service, policy, retry, libcurl transport, reply queues, status strings | `vks_http_*` | 34 distinct, 38 lines in `llcorehttp/httprequest.cpp`; `httpcommon.cpp:240` (`vks_http_status_is_retryable`); `_httpservice.h:52` (`vks_http_service_state`) | `HttpRequest` over the ABI; `HttpOptions`/`HttpHeaders`/`HttpResponse`/`BufferArray` unchanged |
| Asset storage bookkeeping | `vks_asset_*`, `vks_toxic_*` | 9 distinct in `llassetstorage.cpp` (`vks_asset_completion_order`, `…_duplicate_verdict`, `…_error_string`, `…_timed_out_order`, `vks_toxic_*`) | Rung five |
| LLSD bodies of every capability and event-queue exchange | `vks_llsd_*` | 23 lines in `llcommon/llsdserialize.cpp` | Rung two; reached through `LLSDSerialize` from `llcorehttputil` |

Wiring: `llmessage` links `vkstorm_ffi` (`llmessage/CMakeLists.txt:186-196`),
`llcorehttp` links it with libcurl ordered after it
(`llcorehttp/CMakeLists.txt:63-74`). There is no runtime switch and no
C++ fallback. None of the viewer-side networking files (`llviewermessage`,
`llviewerregion`, `lleventpoll`, `llaisapi`, `lllogininstance`,
`llxmlrpctransaction`, `llworld`, `llstartup`) call a `vks_*` function
(`grep -lE '\bvks_\w+\(' newview/*.cpp` lists only `llhttpretrypolicy`,
`llmeshrepository`, `lltexturecache`, `lltexturefetch`,
`llviewerassetstorage`, all from rung five).

The one `vks_msg_*` use in tests (`newview/tests/llinventory_offer_oracle_fakes.cpp:227`,
`vks_msg_zero_code`) is the inventory recorder building zero-coded packets.

### What stays C++

| Part | Location | Why it is still C++ (as recorded in `doc/rust_port.md` or observed) |
|---|---|---|
| `LLMessageSystem` send/receive loops, `LLPacketRing`, sockets | `message.cpp`, `llpacketring.cpp`, `net.cpp` | Rung three judged it I/O and timing |
| `LLCircuit`, acks, resends, ping, trust | `llcircuit.cpp` | Same |
| Template map, handler slots, `callHandler`, trust and ban checks, `LLTemplateMessageDispatcher` | `message.cpp`, `llmessagetemplate.h`, `lltemplatemessagedispatcher.cpp` | Application dispatch on top of the reader |
| `LLSDMessageBuilder`/`Reader`, `LLMsgData` | `llsdmessage*.cpp`, `llmessagetemplate.h` | They build the C++ `LLSD` class |
| `LLHTTPNode` tree, `LLMessageHandlerBridge`, `LLMessageConfig` | `llhttpnode.cpp`, `message.cpp:104-137`, `llmessageconfig.cpp` | Not addressed by any rung |
| Xfer, transfer manager | `llxfer*.cpp`, `lltransfer*.cpp` | Not addressed; rung five kept "the legacy UDP transfers" C++ |
| `LLCoreHttpUtil`, `LLCoprocedureManager` | `llcorehttputil.cpp`, `llcoproceduremanager.cpp` | Rung four: coroutine plumbing on `LLCoros` and `LLSD` |
| `LLHttp`, `LLProxy`, `LLAppCoreHttp::sslVerify`, `HTTPStats` | `llcorehttp`, `llmessage/llproxy.cpp`, `newview/llappcorehttp.cpp` | Rung four: hooks the Rust service calls |
| WebSocket (`llwebsocketmgr`, `lljsonrpcws`) | `llcorehttp` | Separate transport on websocketpp |
| Everything in `newview` listed under Components | | Application logic and consumers |

## Port assessment

Verdicts: **port** (a candidate for a coming rung), **keep** (stays C++ as a
thin layer or by design), **defer** (port only after the subsystems that
depend on it have moved).

| Component | Verdict | Reasons | Boundary candidate | Prerequisites |
|---|---|---|---|---|
| UDP socket, packet ring, `LLCircuit` reliability | defer | Pure I/O and timing; nothing measured says it is slow; correctness depends on timing that the existing tests do not record (no circuit-level corpus exists). Porting it changes the threading story (it runs inline in `idleNetwork`) for no recorded benefit. | Whole-packet in/out: "give me the next decoded packet" / "send these bytes reliably to host" | A recorded packet/ack/resend trace oracle (none exists); a decision on whether the message system gets its own thread |
| Template codec, data packers | done (rung three) | — | Already at `vks_msg_*`/`vks_dp_*` | — |
| Handler slots and dispatch (`setHandlerFunc*`, `callHandler`) | keep | The registry is the seam each later rung uses: a ported subsystem's C++ wrapper keeps its `process_*` function, reads with the Rust-backed getters and forwards to its crate. Moving dispatch into Rust before the handlers move would make every C++ handler a callback across the ABI. | Per-message handler functions | — |
| `LLHTTPNode` tree, `LLMessageHandlerBridge`, `LLSDMessageReader` | keep (short term) | Event-queue messages reach the same handler slots through it; replacing it is only useful once handlers consume Rust LLSD directly. | `LLMessageSystem::dispatch(name, LLSD)` | Rung two's `LLSD` class stays C++ by design, so this stays too |
| `LLMessageConfig`, `message.xml` | keep | Static configuration | — | — |
| Xfer | defer | Still used for task inventory contents (`llviewerobject.cpp:3538`) when `RequestTaskInventory` is absent, mute lists and estate files. Small, self-contained, but its main consumer (task inventory) is in the inventory rung's scope. | `LLXferManager` API (request file, callbacks) | Inventory rung decides whether the UDP task-inventory path survives on Second Life |
| Transfer manager | defer / possibly drop on SL | Only the UDP asset path, which on Second Life is disabled (`llviewerassetstorage.cpp:561`). Inference: an `opensim` feature candidate, like the UDP texture path in rung five. | `LLTransferManager` channels | Decision on OpenSim scope |
| `llcorehttp` service | done (rung four) | — | `vks_http_*` | — |
| `LLCoreHttpUtil` adapters, coprocedure pools | keep | Fibers on the main thread, `LLEventPump`, C++ `LLSD`; nothing to transform. | `HttpCoroutineAdapter` | Only if coroutines are replaced wholesale (not planned in the rung order) |
| Event poll (`LLEventPoll`) | keep, candidate later | 429 lines of retry policy around one long-poll; its behaviour (FS retry settings, timeouts) is product behaviour. The policy part (error counting and backoff) could be a small Rust state machine, like `LLAdaptiveRetryPolicy` in rung five. | `eventPollCoro` loop decisions | A recorded event-poll scenario set against the HTTP mock |
| Capability table and seed request (`LLViewerRegion` impl) | keep | It is a `std::map<std::string,std::string>` plus a coroutine; the value is in the name list, which is data. | `getCapability(name)` | — |
| Login (`LLLoginInstance`, `LLLogin`, `LLXMLRPCTransaction`) | keep | Event-pump plumbing; the XML-RPC response is parsed by `LLXMLNode`. Inference: a candidate only if XML parsing moves to Rust. | — | — |
| `llviewermessage.cpp` handlers | port piecewise with their subsystems | 60 `process_*` functions spanning every subsystem; each moves with the rung that owns its data (objects with the object store, IM offers with inventory, etc.). | One handler per message | The owning rung |
| AISv3 client (`AISAPI`) | port with inventory (the decision part) | The command builders are URL and LLSD body construction, the transport is the coroutine adapter. `AISUpdate` is the parser and model updater that the inventory rung's recorders already cover (`llaisupdate_parity`). | `AISUpdate` input (LLSD) → model changes; commands stay C++ coroutines | Inventory model containers in Rust (the crate's own doc says the model containers stay C++ in this rung) |
| Teleport flow | keep | State machine across `LLAgent`, `LLWorld`, `LLViewerRegion`, UI and RLVa; FS and RLVa changes are dense (`llagent.cpp` 112 `<FS:`, 86 `rlv`). | — | Object store and region rungs |
| Legacy name cache (`LLCacheName`) | keep / defer | UDP `UUIDNameRequest`, heavily Firestorm-patched (19 markers) | — | — |

### What the inventory rung needs from this layer

The inventory rung (rung seven) is already in recording: the crate
`vkstorm-inventory` exists in vkstorm-core-rust (`crates/vkstorm-inventory`,
`#![forbid(unsafe_code)]`, "It is not the inventory model … the model,
cache, fetch and messaging code keeps its containers", `src/lib.rs`), but
`crates/vkstorm-ffi/src/` at `1b183c7` has no inventory module and no
`vks_inv*` symbol is called in this tree. Its needs from networking:

1. **UDP handlers, unchanged in place.** The 9 handlers of
   `LLInventoryModel::registerCallbacks` (`llinventorymodel.cpp:3902-3924`),
   `ReplyTaskInventory`, the inventory-offer branch of
   `ImprovedInstantMessage`, and the `BulkUpdateInventory` method of
   `GenericMessage` (`llinventorymodel.cpp:3599`). They read through the
   Rust-backed template reader; the rung changes their bodies, not the
   registration. The R2 recorder already builds those packets with the
   real builder and reads them with the real reader
   (`newview/tests/fixtures/inventory/msgs/README.md`).
2. **Outgoing UDP.** 25 distinct inventory/task/rez message names are sent
   from `newview` (`MoveInventoryItem` ×4, `UpdateInventoryItem` ×3,
   `UpdateInventoryFolder` ×3, `DeRezObject` ×3, `FetchInventory` ×2,
   `FetchInventoryDescendents`, `CreateInventoryItem`, `CopyInventoryItem`,
   `LinkInventoryItem`, `PurgeInventoryDescendents`, `RequestTaskInventory`,
   `RezObject`, …). These stay `LLMessageSystem` calls from C++ wrappers.
3. **HTTP/caps.** `FetchInventory2`, `FetchInventoryDescendents2`,
   `FetchLib2`, `FetchLibDescendents2` via `LLInventoryModel`'s own
   `HttpRequest` pair on `AP_INVENTORY` (`llinventorymodel.cpp:3580-3594`,
   pumped at `:3607`) and the background fetch; AISv3 via the `AIS`
   coprocedure pool of size 1 (ordering matters: `llcoproceduremanager.cpp:45-49`);
   `CreateInventoryCategory`, `IncrementCOFVersion`, the `Update*Inventory`
   upload caps, `RequestTaskInventory`/`CreateTaskInventoryItem`. All of it is
   already Rust underneath (rung four); the rung needs the coroutine
   adapters to keep working, which is a no-change requirement.
4. **The `use_http_inventory()` switch** (`llviewermenu.cpp:12659`) decides
   whether the fetch and AIS caps are even requested
   (`llviewerregion.cpp:3486`); the R3b recorder keeps the pre-D2 Second
   Life UDP fallback scenarios on (`newview/tests/inventory/r3-fetch.cmake`,
   `VKS_INVFETCH_PRE_D2_GATES`). The rung must decide whether the UDP fetch
   path (`FetchInventoryDescendents` → `InventoryDescendents`) stays.
5. **Login response.** `inventory-root`, `inventory-skeleton`,
   `inventory-lib-*` arrive as LLSD from the XML-RPC login
   (`llstartup.cpp:2654-2726`, `:5025`) and go to `LLInventoryModel::loadSkeleton`.
6. **Xfer for task inventory** when `RequestTaskInventory` is unavailable
   (`llviewerobject.cpp:3131-3201`, `:3437-3562`), covered by the R4 task
   recorder (`llinventory_task_oracle`).

None of these require porting anything in `llmessage` or `llcorehttp`
first. The useful additions on this side would be (Inference) a test-only
way to inject an event-queue message through `LLMessageSystem::dispatch`
(the offer and bulk-update paths can arrive either way) and keeping the mock
AIS/caps server (`newview/tests/vksinventory_mock_server.py`, 992 lines) in
step with the HTTP parity mock.

### What the object-store rung needs from this layer

1. **The object update handlers**: `ObjectUpdate`, `ObjectUpdateCompressed`,
   `ObjectUpdateCached`, `ImprovedTerseObjectUpdate`, `KillObject`
   (`llviewermessage.cpp:4562-4640` → `LLViewerObjectList::processObjectUpdate`
   `llviewerobjectlist.cpp:442`, `processCompressedObjectUpdate` `:755`,
   `processCachedObjectUpdate` `:762`, `processUpdateCore` `:242`), plus
   `ObjectProperties`, `ObjectPropertiesFamily`, `NameValuePair`,
   `ObjectAnimation`, `ObjectPhysicsProperties` (LLSD node,
   `llviewerobject.cpp:8266`) and `GenericStreamingMessage` (GLTF overrides).
   These are the hottest decode paths: rung three measured exactly this
   getter sequence for `ObjectUpdate` (`doc/rust_port.md`, rung three,
   Measurements).
2. **`LLDataPackerBinaryBuffer`**, whose fixed-size copies stay inline C++
   by measurement; the compressed and cached update paths and
   `LLViewerRegion::cacheFullUpdate` (`llviewerregion.cpp:2927`, `:2982`)
   use it. An object-store crate that decodes `ObjectUpdateCompressed`
   itself would replace these calls rather than wrap them.
   Inference: the cleanest boundary is "decoded packet slots in, object
   records out", i.e. hand the Rust reader's slot table to the object
   store without returning through the C++ getters; that needs a new
   `vks_msg_*` entry point exposing the reader's slots.
3. **Cache misses**: `RequestMultipleObjects` from
   `LLViewerRegion::requestCacheMisses` (`llviewerregion.cpp:3144`, sent at
   `:3160`) and `LLWorld::requestCacheMisses` (`llworld.cpp:1538`).
4. **Capabilities**: `InterestList`, `GetObjectCost`,
   `GetObjectPhysicsData`, `ResourceCostSelected`, `ObjectMedia`,
   `ModifyMaterialParams`, `RenderMaterials`, `ObjectAnimation`.
5. **Region identity**: region handle, `LLHost` and the region handshake
   (`unpackRegionHandshake`, cache flags) that select the object cache.

## Existing tests and oracles

| Test | Where | What it covers | Oracle | In CI focused list (`.github/workflows/build.yaml:217-225`) |
|---|---|---|---|---|
| `INTEGRATION_TEST_llmessagetemplate` | `llmessage/tests/llmessagetemplate_test.cpp`, corpus `llmessage/tests/fixtures/llmessage_corpus.vksm` (10.2 MB) | Template parse, 1912 packed messages, 7393 reads, zero-coding, data packers through the real C++ classes | Corpus recorded from the pre-port C++ | yes |
| `llmessage` unit/integration tests | `llmessage/CMakeLists.txt:199-226`: `llcoproceduremanager`, `llnamevalue`, `lltrustedmessageservice`, `lltemplatemessagedispatcher` (unit); `llhost`, `llpartdata`, `llxfer_file` (integration); `llavatarnamecache` commented out | LL upstream unit tests | — | no |
| `llmessage/tests/llhttpclient_test.cpp` | present but not listed in `CMakeLists.txt` | Legacy `LLHTTPClient` | — | not built |
| `lltest` (legacy) | `indra/test/`: `message_tut.cpp`, `lltemplatemessagebuilder_tut.cpp`, `llsdmessagebuilder_tut.cpp`, `llsdmessagereader_tut.cpp`, `llmessageconfig_tut.cpp`, `llhttpnode_tut.cpp`, `lliohttpserver_tut.cpp`, `llservicebuilder_tut.cpp`, `llbuffer_tut.cpp`, `io.cpp`; `llmessagetemplateparser_tut.cpp` Linux only; `lldatapacker_tut.cpp` not compiled | Message system, LLSD messages, HTTP node tree, IO pipes | — | built with the tree |
| `INTEGRATION_TEST_llcorehttp_parity` | `llcorehttp/tests/llcorehttp_parity_test.cpp`, mock `tests/vkshttp_mock_server.py`, fixture `tests/fixtures/llcorehttp_parity.xml` | 16 wire scenarios (methods, ranges, retries, timeouts, redirects, concurrency, cancel, cookies, bodies, policy options, pipelining) | Record from the pre-port C++ | yes |
| `INTEGRATION_TEST_llcorehttp` | `llcorehttp/tests/llcorehttp_test.cpp` + `test_*.hpp`, peer `test_llcorehttp_peer.py` | Original LL suite; 2 known failures by its own construction (`doc/rust_port.md`, rung four) | — | no |
| `INTEGRATION_TEST_jsonrpcws`, `INTEGRATION_TEST_wsserver` | `llcorehttp/tests/` | WebSocket JSON-RPC | — | yes |
| `INTEGRATION_TEST_llhttpretrypolicy` | `newview/tests/llhttpretrypolicy_test.cpp` | `LLAdaptiveRetryPolicy` (Rust-backed) | — | yes |
| `lllogininstance_test`, `llxmlrpclistener_test` (+ `test_llxmlrpc_peer.py`), `lllogin_test` | `newview/tests/`, `viewer_components/login/tests/lllogin_test.cpp` | Login instance with stubs; XML-RPC listener; login coroutine | — | not in the focused list |
| `INTEGRATION_TEST_llinventorymodel_parity` (R2) | `newview/tests/llinventorymodel_parity_*.{cpp,h}`, `tests/inventory/r2.cmake`, fixtures `tests/fixtures/inventory/msgs/set8_handlers.txt`, `…/cache` | Inventory UDP handlers through the real `gMessageSystem` builder and reader | Recorded from the viewer C++ before any port commit (README) | yes |
| `INTEGRATION_TEST_llinvfetch_parity` (R3b) | `newview/tests/llinvfetch_parity_*.cpp` (4,892 lines incl. test), mock `tests/vksinventory_mock_server.py`, fixtures `tests/fixtures/inventory/ais/fetch` | Real `llaisapi.cpp` and background fetch, real coroutines, coprocedure pool and `llcorehttp`, against a loopback AIS/caps server; request log, side effects, final model | Recorded from unmodified C++ (README) | yes |
| `INTEGRATION_TEST_llaisupdate_parity` (R3) | `newview/tests/llaisupdate_parity_test.cpp`, fixtures `tests/fixtures/inventory/ais/update` | `AISUpdate` parsing and model application | Same | yes |
| `INTEGRATION_TEST_llinventory_offer_oracle`, `INTEGRATION_TEST_llinventory_task_oracle` (R4) | `newview/tests/llinventory_offer_oracle*`, `llinventory_task_oracle*`, fixtures `tests/fixtures/inventory/offer`, `…/task` (`cap_replies.txt`) | Offers over IM, task inventory over caps and xfer | Frozen copies of the C++ (`*_frozen.cpp`) | yes |
| `INTEGRATION_TEST_llinventory_parity`, `llinventory_runtime` | `llinventory/tests/`, fixtures `c1…c10` incl. `c9_messages.txt` | Inventory records, permissions, LLSD and message forms | `llinventory_oracle.cpp` | yes |
| Crate tests | vkstorm-core-rust `crates/vkstorm-messages` (19 unit tests, `tests/corpus.rs`), `crates/vkstorm-http` (15 unit, `tests/scenarios.rs` with 16 scenarios) | Codec and HTTP on the crate side | Same corpora | crate CI |

Not covered by any recorded oracle: circuits, acks and resends, ping and
timeouts, `UseCircuitCode` retries, the event poll's retry and timeout
handling, the seed capability request, `LLMessageHandlerBridge` dispatch of
LLSD messages (except where the inventory recorders inject them), xfer
beyond `llxfer_file`, the transfer manager, and teleport.

## Risks and open questions

1. **Template message count.** `grep` finds 483 message headers in
   `scripts/messages/message_template.msg`; `doc/rust_port.md` records the
   parser reading 478 and the corpus holding 478 parsed messages. Either the
   template gained messages after the corpus was recorded (the header
   comment's "current highest" numbers, Low 431 / Medium 18 / High 30, do
   not match the counted 431 / 17 / 32 either) or some headers are not
   parsed. Not determined here; it matters because the corpus is the
   rung-three oracle.
2. **Single handler slot per message.** `setHandlerFunc*` overwrites
   (`llmessagetemplate.h:382`). `PayPriceReply` and `TelehubInfo` are
   installed and cleared by floaters at run time. A ported subsystem that
   registers its own handler for a shared message (`ImprovedInstantMessage`
   carries chat, IM and inventory offers; `GenericMessage` carries several
   methods) must chain inside the existing C++ handler rather than register
   a second one.
3. **Two delivery paths, one handler.** The same `process_*` function runs
   for a UDP packet (template reader) and an event-queue message (LLSD
   reader via `LLMessageHandlerBridge`). Parity tests that drive only the
   UDP path do not cover the LLSD reader's getters (`LLSDMessageReader`,
   C++). `message.xml` marks `TeleportFinish`, `EnableSimulator`,
   `CrossedRegion`, `ParcelProperties`, `ScriptRunningReply` and others as
   `llsd`.
4. **Duplicate per-frame work.** `gXferManager->retransmitUnackedPackets()`
   and `gAssetStorage->checkForTimeouts()` run in `processAcks`
   (`message.cpp:774-779`) and again in `idleNetwork`
   (`llappviewer.cpp:6599-6601`). Harmless today (Inference), but a port of
   either must reproduce or deliberately settle the call count.
5. **Event poll lifetime.** `LLViewerRegionImpl` holds a raw `LLEventPoll*`
   (`delete`d and replaced in `setCapability`, `llviewerregion.cpp:3646-3648`);
   events are posted to the main work queue capturing `this` of the impl
   (`lleventpoll.cpp:398`). Whether the impl's `shared_ptr` keeps it alive
   across a region teardown was not verified.
6. **Capabilities requested but unused, and used but not requested.**
   `NewAgentInventory` and `FlickrConnect` are looked up but never
   requested; the request list includes names with no literal use site
   (they may be used through constants or `requestPostCapability`). A
   per-name consumer map was built here from literals only.
7. **Firestorm divergence in the transport.** `lleventpoll.cpp` restores
   pre-coroutine behaviour (60 s timeout, no retries) and adds
   `FSEventPollCoreRetries`; `message.cpp` restores "original HTTP options
   for OpenSim". Any oracle recorded against LL upstream behaviour would
   differ; the recorders must run against this tree's C++.
8. **OpenSim scope.** UDP assets (`[UDP Assets]`), `GetTexture`/`GetMesh`,
   Aurora region sizes, `DispatchOpenRegionSettings`, `VETPBR` and the
   OpenSim login options are compiled under `OPENSIM` or gated on
   `isInSecondLife()`. The rung plan uses a crate `opensim` feature; which
   of the transfer manager and xfer paths become OpenSim-only is open.
9. **No circuit-level oracle.** Nothing records packet ids, ack timing or
   resends. If the message loop is ever ported or threaded, an oracle has to
   be built first (Inference: a loopback simulator stub around
   `LLMessageSystem`, as `vkshttp_mock_server.py` is for HTTP).
10. **Threading assumptions.** Handlers, coroutines, `LLSD` bodies and the
    capability table are main-thread only; the HTTP service is a Rust
    thread with replies drained by `update()` on the owner's thread.
    Moving any handler's work into a Rust thread would require a copy of
    the decoded message, since the reader's slots are reused per packet.
11. **Not determined:** the exact set of `llsd`-flavor messages the server
    actually sends on Second Life today (only the viewer's `message.xml`
    was read); whether `LLTrustedMessageService` and the `llsdappservices`
    nodes are reachable at all in the viewer (no callers found); the
    runtime frequency of any message (nothing measured).
