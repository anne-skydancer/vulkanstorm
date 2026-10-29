# 06 — Viewer application core and features

This chapter covers the part of `indra/newview` that holds the viewer together:
the application object and its platform variants, the startup state machine, the
main loop and idle step, settings, the agent and camera, the world/region/parcel
data side, the logic of chat, IM, groups and friends, teleport, pathfinding, the
scripting features, RLVa, the Firestorm feature modules, the small Vulkanstorm
delta, and the global singletons that couple all of it. It also contains the
module map that assigns all 837 top-level `.cpp` files in `indra/newview` to a
functional cluster and to the survey chapter that owns it. Most of this code is
glue. It reaches into every other subsystem through process-wide globals (`gAgent`
alone appears 4,074 times in 367 files), so it can move to Rust only late, after the
rungs beneath it. The same code also contains a set of leaf parsers and state
machines (SLURLs, chat-log lines, RLVa command syntax, version strings, maturity
rules) that could be ported early as pure functions with oracle tests.

Snapshot: branch `codex/rust-inventory-model`, HEAD `8d101eccb6`, 27 October 2026.
All counts come from the working tree. Unless a command says otherwise, they cover
top-level `indra/newview/*.cpp` and `*.h` and leave out `tests/`.

Chapter numbering assumed across the survey: 01 foundations (llcommon etc.),
02 networking, 03 content/inventory/object model, 04 rendering, 05 UI,
06 application core (this chapter), 07 media/audio/plugins, 08 build/CI. No
`newview` source cluster belongs to 08.

---

## Scope and size

| Measure | Value | Command |
|---|---|---|
| Top-level `.cpp` files in `indra/newview` | 837 | `ls indra/newview/*.cpp \| wc -l` |
| Lines in those `.cpp` files | 732,090 | `cat indra/newview/*.cpp \| wc -l` |
| Top-level `.h` files | 854 | `ls indra/newview/*.h \| wc -l` |
| Lines in those `.h` files | 145,305 | `cat indra/newview/*.h \| wc -l` |
| `.mm` (macOS Objective-C++) | 3 files, 717 lines | `cat indra/newview/*.mm \| wc -l` |
| `indra/newview/tests` (`.cpp`/`.h`/`.py`) | 96 entries, 45,248 lines | `wc -l tests/*.cpp tests/*.h tests/*.py` |
| `ll*.cpp` (LL naming; mostly LL upstream) | 687 files, 617,024 lines | `cat ll*.cpp \| wc -l` |
| `fs*.cpp` (Firestorm) | 95 files, 64,577 lines | `cat fs*.cpp \| wc -l` |
| `rlv*.cpp` (RLVa) | 11 files, 12,916 lines | `cat rlv*.cpp \| wc -l` |
| Other prefixes (`ao*`, `lgg*`, `NACL*`, `vj*`, `pipeline`, ...) | 44 files, 37,573 lines | see below |
| `.cpp` files present but not compiled (no live entry in `CMakeLists.txt`) | 10 files, 2,619 lines | see [Risks](#risks-and-open-questions) |

Of the 44 "other prefix" files, five come from LL upstream: `pipeline.cpp`,
`gltfscenemanager.cpp`, `groupchatlistener.cpp`, `noise.cpp` and `bugsplatattributes.cpp`
(their license headers name Linden Research, or they include only `linden_common.h`).
The remaining 39 are Firestorm or third-party contributions (Phoenix/Exodus/LGG/NACL/
Vir/AO). Inference: some `ll*`-prefixed files are also Firestorm-era additions or LL
legacy code that Firestorm revived (for example `llpaneldir*.cpp` and `llchatbar.cpp`).
The prefix therefore gives a lower bound on fork-specific code, not an exact figure.

Fork markers inside LL files:

| Marker | Lines | Files | Command |
|---|---|---|---|
| `<FS` (Firestorm change blocks) in all `.cpp`/`.h` | 7,308 | 715 | `grep -c -- '<FS' *.cpp *.h` |
| `<FS` inside `ll*` files only | 6,809 | 611 of 1,379 | `grep -l '<FS' ll*.cpp ll*.h \| wc -l` |
| `RLVa` tag lines outside `rlv*` | 1,647 | — | `cat $(ls *.cpp *.h \| grep -v ^rlv) \| grep -c RLVa` |
| `Vulkanstorm`/`<VulkanStorm>` text | 29 | 12 | `grep -c -i vulkanstorm *.cpp *.h` |

Most frequent `<FS:` authors: Ansariel 3,311, Beq 780, Zi 630, CR 527, PP 466
(`grep -ho '<FS:[A-Za-z]*' *.cpp *.h | sort | uniq -c | sort -rn`).

Most Firestorm-marked LL files: `llfloaterpreference.cpp` 194, `llviewermenu.cpp` 174,
`llvoavatar.cpp` 171, `llviewermessage.cpp` 145, `llstartup.cpp` 144,
`llappviewer.cpp` 144, `llagent.cpp` 120.

### Vulkanstorm delta in newview

The clean release base is Firestorm 7.2.4, commit `10bd3c9f93` (`doc/clean_release_base.md`).
Diff against it:

| Scope | Files | Insertions | Deletions | Command |
|---|---|---|---|---|
| Top-level `newview/*.cpp`/`*.h` (non-test) | 104 (9 added) | 10,927 | 5,097 | `git diff --numstat 10bd3c9f93 HEAD -- 'indra/newview/*.cpp' 'indra/newview/*.h'` |
| `newview/tests/*` | 48 | 33,004 | 2 | same, filtered on `tests/` |
| Commits touching `indra/newview` since base | 113 | — | — | `git log --oneline 10bd3c9f93..HEAD -- indra/newview \| wc -l` |

The nine added source files are `llscripteditorws.cpp/.h`, `llpublishedobjectmgr.cpp/.h`,
`llfloaterscripting.cpp/.h` (a port of LL Viewer 26.4's SLua editing and IDE bridge,
commit `c46d2bacc6`; the code carries LL 2025 copyright headers),
`llscripteditorposixfile.h` (`b5781b2e47`), `llgraphicsidentity.h` (`d12031fb24`, Mesa
Zink backends) and `llversionparser.h` (`734e56aeee`). None uses a `vks*` file prefix. No
`vks*.cpp` exists in `indra/newview` (`ls indra/newview | grep -i vks` matches only test
mock servers in `tests/`).

---

## newview module map

The classifier (`cluster.py`, reproduced in [State commands](#state-commands)) applies
ordered filename regexes to every top-level `.cpp`. The first match wins, and the last
UI rule catches whatever is left. All 837 files are assigned. Line counts are Python
line counts and differ from `wc -l` by 2 in total (732,092 against 732,090) because
two files lack a trailing newline. "FS-prefix files" counts files whose names start
with a Firestorm/third-party prefix (`fs`, `rlv`, `ao`, `lgg`, `exo`, `NACL`, `vj`,
`omni`, `qtool`, `pie`, `quickprefs`, ...).

| Cluster | Key | Files | Lines | FS-prefix files | Owner |
|---|---|---|---|---|---|
| RLVa | `rlv` | 11 | 12,916 | 11 | 06 |
| Scripting: LSL/SLua editor, preprocessor, bridge, compile queue | `scripting` | 21 | 18,310 | 5 | 06 |
| Pathfinding | `pathfinding` | 16 | 6,880 | 0 | 06 |
| LLEventAPI listeners (automation surface) | `listeners` | 19 | 3,721 | 1 | 06 |
| Application lifecycle, platform, crash, updater | `lifecycle` | 12 | 13,055 | 2 | 06 |
| Startup, login, grids | `startup` | 8 | 9,033 | 2 | 06 |
| Settings, presets, input bindings | `settings` | 13 | 11,642 | 1 | 06 |
| Agent, camera, movement, gestures, autopilot | `agent` | 21 | 18,162 | 0 | 06 |
| Avatar appearance, wearables, outfits, AO | `appearance` | 29 | 27,745 | 4 | 03 |
| Firestorm poser and avatar alignment | `poser` | 13 | 10,986 | 13 | 06 |
| World, regions, parcels, land, estate, world map data | `world` | 40 | 31,994 | 3 | 06 |
| Navigation: SLURLs, URL dispatch, teleport history, landmarks, maps | `navigation` | 35 | 25,257 | 4 | 06 |
| Chat, IM, groups, friends, people, moderation (logic) | `comms` | 53 | 40,831 | 20 | 06 |
| Experiences, marketplace, economy, search, external web services | `services` | 54 | 22,527 | 8 | 06 |
| Statistics, performance accounting, diagnostics | `stats` | 23 | 11,501 | 3 | 06 |
| Firestorm/third-party import, export, local mesh, area search | `fsexport` | 19 | 15,620 | 19 | 06 |
| Message dispatch, HTTP/XML-RPC/event poll, security, asset transport | `net` | 17 | 17,861 | 0 | 02 |
| Inventory model and inventory UI | `inventory` | 43 | 57,235 | 2 | 03 |
| Object model, mesh, materials, local assets, selection | `objects` | 39 | 82,160 | 0 | 03 |
| Texture fetch, cache, viewer textures | `textures` | 11 | 12,688 | 0 | 03 |
| Rendering: pipeline, draw pools, partitions, environment, terrain, sky, HUD drawing | `render` | 74 | 75,888 | 1 | 04 |
| Media, audio, voice, streaming | `media` | 45 | 34,644 | 8 | 07 |
| UI: build tools, manipulators, object edit panels | `ui_build` | 36 | 43,822 | 5 | 05 |
| UI: chat, IM, conversations, people, groups, profiles | `ui_comms` | 87 | 58,133 | 23 | 05 |
| UI: notifications, toasts, system well | `ui_notify` | 28 | 9,325 | 4 | 05 |
| UI: viewer window, menus, floater registry, bars, preferences, snapshots, misc | `ui_shell` | 70 | 60,156 | 8 | 05 |
| **Total** | | **837** | **732,092** | | |

By owning chapter:

| Chapter | Files | Lines |
|---|---|---|
| 02 networking | 17 | 17,861 |
| 03 content/inventory/object model | 122 | 179,828 |
| 04 rendering | 74 | 75,888 |
| 05 UI | 221 | 171,436 |
| 06 application core (this chapter) | 358 | 252,435 |
| 07 media/audio/plugins | 45 | 34,644 |

Judgement calls in the assignment:

- Feature floaters go with their feature when this chapter owns the feature: the
  pathfinding, region/land, experience, marketplace, scripting and poser floaters are
  counted under 06. Generic chat/people floaters go to 05.
- `llselectmgr.cpp` (9,345 lines) is counted under 03 objects, since selection state is
  object-model data. Its build-tool UI consumers are under 05.
- `llviewercamera.cpp` is counted under 04 (frustum and culling). `llagentcamera.cpp`
  (camera control) is counted under 06.
- `llaisapi.cpp` (AIS inventory HTTP) is counted under 02 transport. The inventory model
  that consumes it is under 03.
- `llviewerwindow.cpp` and `llviewermenu.cpp` are counted under 05. Both are
  integration points of this chapter's code (see [Coupling](#coupling)).
- `fspanellogin.cpp` and `llpanellogin.cpp` (login UI) are counted under 05.
  `llstartup.cpp` and `lllogininstance.cpp` are counted under 06.

### Files per cluster for this chapter (largest first, `.cpp` lines)

- **lifecycle (12)**: llappviewer 7220, llappviewerwin32 1457, llvelopack 1248,
  llcommandlineparser 733, llappviewerlinux 616, llmachineid 511, llappviewermacosx 471,
  llversioninfo 312, llwindebug 201, bugsplatattributes 131, sanitycheck 100,
  llhasheduniqueid 55.
- **startup (8)**: llstartup 5339, fsgridhandler 1493, llviewernetwork 707,
  lllogininstance 663, lfsimfeaturehandler 380, llloginhandler 191, llfirstuse 183,
  llviewerhome 77.
- **settings (13)**: quickprefs 2219, llviewerinput 1984, llviewerjoystick 1702,
  llviewercontrol 1631, llkeyconflict 982, llpresetsmanager 750, llfloatersettingsdebug 693,
  llfloaterjoystick 618, llsetkeybinddialog 377, llfloatersettingscolor 334,
  llfloater{save,delete,load}prefpreset 125/117/110.
- **agent (21)**: llagent 6689, llagentcamera 3392, llgesturemgr 1654, llfloatercamera 1017,
  lltracker 984, llfollowcam 884, llmoveview 811, llagentbenefits 409, llagentpilot 405,
  llagentui 332, llfloaterhoverheight 261, llviewergesture 212, llagentaccess 182,
  llfloatersavecamerapreset 181, llemote 165, llfloatercamerapresets 161,
  llagentpicksinfo 156, lldelayedgestureerror 123, llagentlanguage 76, llagentdata 34,
  llanimstatelabels 34.
- **world (40)**: llfloaterregioninfo 4850, llviewerregion 4109, llfloaterland 3888,
  llviewerparcelmgr 2880, llworld 1975, llpanelgrouplandmoney 1668, llfloatergodtools 1370,
  llfloaterbuyland 1366, llworldmap 726, lltoolbrush 721, llfloatertopobjects 658,
  llfloaterauction 567, llfloatersellland 559, llfloaterregionrestartschedule 386,
  llfloaterobjectweights 368, llpanellandaudio 357, llfloaterlandholdings 355,
  llpanellandmedia 333, llworldmapmessage 324, alfloaterregiontracker 299, llworldmipmap 282,
  llwind 281, llfloatertelehub 279, llpanelland 275, llfloateropenobject 269,
  llfloaterregionrestarting 260, llregioninfomodel 254, llremoteparcelrequest 248,
  llestateinfomodel 238, lltoolselectland 237, fsworldmapmessage 214,
  llfloaterregiondebugconsole 211, llpanelopenregionsettings 210, llaccountingcostmanager 208,
  llvlmanager 206, fsregioncross 195, llviewerlayer 100, llregionposition 94,
  llfloaterbanduration 92, llparcelselection 82.
- **navigation (35)**: llfavoritesbar 2504, llnetmap 2337, llfloaterworldmap 2199,
  llworldmapview 1988, llpanelplaces 1490, llpanelteleporthistory 1420,
  lllocationinputctrl 1418, llpanellandmarks 1347, llnavigationbar 1003,
  fsfloaterplacedetails 942, fsslurl 718, llpanelplaceprofile 716, lllandmarkactions 565,
  llpanellandmarkinfo 554, llurldispatcher 552, llslurl 542, llpaneltopinfobar 522
  (not compiled), llfloatercreatelandmark 454, llpanelplaceinfo 401,
  llteleporthistorystorage 388, llteleporthistory 382, llfloatermap 312, fsslurlcommand 297,
  llurl 295, llcommandhandler 286, lllandmarklist 273, llurlfloaterdispatchhandler 214,
  llplacesinventorybridge 199, lllocationhistory 179, llsearchhistory 159, llurlhistory 147,
  fsfloaterteleporthistory 137, llplacesinventorypanel 133, llpanelplacestab 99,
  llplacesfolderview 85.
- **comms (53)**: llimview 5251, llimprocessing 2595, llgroupmgr 2586, llavataractions 2567,
  chatbar_as_cmdline 1878, lllogchat 1446, lggcontactsets 1307, lltranslate 1303,
  llmutelist 1255, llcallingcard 1100, fsradar 1083, llspeakers 1081, fsdata 1077,
  llfloaterreporter 1073, fsparticipantlist 1016, llgroupactions 923, fsnearbychathub 882,
  llavatarpropertiesprocessor 878, llautoreplace 831, llconversationmodel 766,
  llfriendcard 733, llconversationlog 710, NACLantispam 687, llchatbar 656, fscommon 617,
  fsgrouptitleregionmgr 560, fsassetblacklist 405, llviewerchat 382, fsnearbychatcontrol 359,
  lleventnotifier 354, lldonotdisturbnotificationstorage 348, llspeakingindicatormanager 330,
  llparticipantlist 309, llchannelmanager 291, permissionstracker 264, fsconsoleutils 263,
  llviewerdisplayname 245, fsradarentry 236, lldateutil 224, llpersistentnotificationstorage 207,
  exogroupmutelist 202, fsmoneytracker 200, fskeywords 168, llnotificationstorage 158,
  llviewerhelp 157, fsfavoritegroups 133, fsavatarrenderpersistence 125, llrecentpeople 121,
  llfloaterevent 119, llcommunicationchannel 113, llconfirmationmanager 112,
  fsnearbychatvoicemonitor 78, llviewerhelputil 67.
- **scripting (21)**: llpreviewscript 3652, llscripteditorws 3124, fslslbridge 1737,
  fslslpreproc 1623, llpublishedobjectmgr 1449, llcompilequeue 1212, llscriptfloater 1156,
  llfloaterscriptlimits 943, llscripteditor 708, llsyntaxid 584, llfloaterscriptdebug 374,
  llfloaterscriptrecover 365, llfloatermyscripts 307, llfloatersearchreplace 230,
  llfloatergotoline 184, lltoastscriptquestion 153, llfloaterscripting 132,
  lltoastscripttextbox 132, fsscriptlibrary 102, fslslbridgerequest 79, fslslpreprocviewer 64.
- **pathfinding (16)**: llfloaterpathfindingconsole 1275, llfloaterpathfindingobjects 916,
  llpathfindingmanager 916, llfloaterpathfindinglinksets 827, llpathfindingpathtool 474,
  llpathfindingnavmeshzone 412, llpathfindinglinkset 408, llfloaterpathfindingcharacters 326,
  llmenuoptionpathfindingrebakenavmesh 261, llpathfindingnavmesh 215,
  llpathfindinglinksetlist 213, llpathfindingobject 200, llpathfindingnavmeshstatus 145,
  llpathfindingobjectlist 122, llpathfindingcharacter 99, llpathfindingcharacterlist 71.
- **services (54)**: fsfloatersearch 3162, llmarketplacefunctions 1903, llpaneldirbrowser 1222,
  llfloatermarketplacelistings 1092, llfloaterflickr 988, llfloaterexperienceprofile 940,
  fsfloaterprimfeed 939, llfloaterpay 670, llcurrencyuimanager 637, llpanelclassified 580,
  llflickrconnect 541, llfloaterbuycurrency 461, fsprimfeedauth 449,
  llpanelexperiencepicker 449, llfloaterexperiences 418, llpanelmarketplaceinboxinventory 399,
  llfloaterbuy 350, llfloatertos 343, fsdiscordconnect 339, llfloaterbuycontents 304,
  llpanelmarketplaceinbox 304, llexperiencelog 300, fsfloaterdiscord 296, llwebprofile 283,
  exoflickr 275, and 29 smaller files (`llpaneldir*`, `llfloaterdirectory`, `llweb`,
  `llfloatergridstatus`, `llbuycurrencyhtml`, `llproductinforequest`, `llclassifiedinfo`, ...).
- **stats (23)**: llfloateruipreview 1765, llfasttimerview 1730, fsfloaterperformance 1189,
  llviewerstats 1032, llscenemonitor 764, llfloaterperformance 755 (not compiled),
  llperfstats 559, llavatarrendernotifier 514, llsceneview 440,
  llavatarrenderinfoaccountant 434, llfloaterlagmeter 399, fsfloatervramusage 338,
  llviewerstatsrecorder 313, and 10 smaller debug floaters.
- **poser (13)** and **fsexport (19)**: see [Firestorm modules](#firestorm-feature-modules).
- **listeners (19)**: llagentlistener 833, llwindowlistener 523, llxmlrpclistener 366,
  llinventorylistener 309, llviewercontrollistener 222, llpipelinelistener 216,
  llgesturelistener 159, llappearancelistener 158, llviewerwindowlistener 113,
  llfloaterimnearbychatlistener 103, fsnearbychatbarlistener 102, groupchatlistener 102,
  lluilistener 101, llcommanddispatcherlistener 82, llstartuplistener 75,
  llurldispatcherlistener 75, llappviewerlistener 63, llvoinventorylistener 63,
  llpanelloginlistener 56.

---

## Components

### Application lifecycle (LL upstream, Firestorm and Vulkanstorm patches)

**Responsibility.** Process entry, configuration, subsystem initialisation, the per-frame
loop, quit and logout, crash and freeze reporting, multi-instance SLURL hand-off, cache
versioning and purge.

**Key files.**

| Item | Location |
|---|---|
| `class LLAppViewer : public LLApp` | `indra/newview/llappviewer.h:85` |
| Windows entry `WINMAIN`/`wWinMain` | `llappviewerwin32.cpp:492`, `:682`; app created `:565`, `init()` `:573`, loop `while (!viewer_app_ptr->frame())` `:625`, `cleanup()` `:649` |
| Linux entry `main` | `llappviewerlinux.cpp:149`; `init()` `:166`, loop `:180`, `cleanup()` `:190` |
| macOS entry | `llappviewermacosx.cpp:265`; frame driven from the Cocoa delegate, `gViewerAppPtr->frame()` `:124` |
| `LLAppViewer::init()` | `llappviewer.cpp:861`–`1481` |
| `initConfiguration()` (settings files, command line) | `llappviewer.cpp:3020` |
| `initThreads()` | `llappviewer.cpp:2597` |
| `initWindow()` | `llappviewer.cpp:3720` |
| `initCache()` | `llappviewer.cpp:5201` |
| `frame()` / `doFrame()` | `llappviewer.cpp:1522` / `:1562` |
| `idle()` | `llappviewer.cpp:5753`–`6219` |
| `idleShutdown()`, `sendLogoutRequest()`, `disconnectViewer()` | `:6219`, `:6317`, `:6622` |
| `cleanup()` | `llappviewer.cpp:1998`–`2586` |
| `requestQuit()`, `userQuit()`, `forceQuit()`, `fastQuit()`, `earlyExit()` | `:4976`, `:5055`, `:4955`, `:4960`, `:5077` |
| Marker files (previous-run crash/freeze detection) | `processMarkerFiles()` `:4736`, `createErrorMarker()` `:6440`, `removeMarkerFiles()` `:4905` |
| Main-loop watchdog | `initMainloopTimeout()` `:6864`, `pingMainloopTimeout()` `:6899`; `new LLWatchdogTimeout("mainloop")` `:1440` |
| Deliberate crash triggers (Develop menu) | `forceErrorLLError()` `:6718` … `forceExceptionThreadCrash()` `:6842` |
| Second-instance SLURL forwarding | `sendURLToOtherInstance()` `:739` |
| Login completion hook | `handleLoginComplete()` `:6932` (called from `llstartup.cpp:3413`) |
| Discord rich presence (FS) | `initDiscordSocial()` `:7123`, `updateDiscordActivity()` `:7133` |
| BugSplat crash reporting (Windows) | `llappviewerwin32.cpp:82`–`160` (`#ifdef LL_BUGSPLAT`) |
| Linux crash reporting | `LLAppViewerLinux::initCrashReporting()` `llappviewerlinux.cpp:446` |
| Velopack updater (LL upstream; off by default) | `llappviewerwin32.cpp:497` (`#if LL_VELOPACK`); `indra/cmake/Velopack.cmake:8` `option(USE_VELOPACK ... OFF)` |

**`init()` order** (line numbers in `llappviewer.cpp`). Default skin folder `:889`.
`initConfiguration()` `:980`. `initMaxHeapSize()` `:989`. `initThreads()` `:1019`.
LLUI/image list/spell checker `:1024`–`1047`. `initStrings()` `:1062`.
`LLWearableType`/`LLSettingsType`/`LLNotifications` `:1068`–`1073`. Floater registration
`LLViewerFloaterReg` `:1131`. `settings_setup_listeners()` `:1156`. `initSLURLHandler()` `:1164`.
`initHardwareTest()` `:1166`. `initCache()` `:1183`. **`selectGLBackend()` `:1200`
(Vulkanstorm)**. `initWindow()` `:1201`. GL info and the feature table
(`LLFeatureManager`) `:1221`–`1307`. `initializeSecHandler()` `:1345`. Coprocedure
manager `:1425`. Voice `:1447`. `loadKeyBindings()` `:1458`.

**Threading.** `initThreads()` (`:2597`) creates `LLLFSThread`, `LLImageDecodeThread`
(the thread count is `clamp(cores-4, 2, 8)`, which `FSImageDecodeThreads` overrides),
`LLTextureCache`, `LLTextureFetch`, a 3-thread `LL::ThreadPool("General")` (`:2592`),
`LLPurgeDiskCacheThread`, optionally `LLFastTimerLogThread`, `gMeshRepo.init()` and the
file/dir picker threads. Everything else in this chapter runs on the main thread,
including every coroutine. `LLCoros::instance().launch` appears 136 times in newview,
and the coroutines are resumed from `llcoro::suspend()` in `doFrame()` (`:1690`).
Cross-thread hand-off to the main thread uses `WorkQueue gMainloopWork("mainloop", ...)`
(`:413`), drained in `idle()` with a time budget (`gMainloopWork.runFor(...)` `:5795`).
`postToMainCoro()` (`:6435`, 17 call sites) and `addOnIdleCallback()` (`:5392`) post work to it.

**Global state** owned here (`llappviewer.h:417`–`468`): `gFrameCount`, `gFrameTime`,
`gFrameTimeSeconds` (63 uses), `gFrameIntervalSeconds`, `gFPSClamped`, `gFrameDTClamped`,
`gDisconnected` (84 uses in 43 files), `gDebugInfo`, `gLastExecEvent`, `gServicePump`,
`gLogoutTimer`, `gSimulateMemLeak`, and others. `llappviewer.h` declares 36 `extern g*` lines.

**Vulkanstorm changes here.** `selectGLBackend()` is per platform
(`llappviewerwin32.cpp:1097`–`1179`, `llappviewerlinux.cpp:104`). It reads `RenderBackend`
and preloads either the bundled Mesa `opengl32.dll`/`libgallium_wgl.dll` (Zink) or
native OpenGL. `initWindow()` (`llappviewer.cpp:3730`–`3758`) maps the value Vulkan to
OpenGL with a warning, keeps Zink on the GL path, and logs the backend. The crash dialog
text says "Vulkanstorm" (`:2682`). `LLWebsocketMgr::instance().update()` is called in
`idle()` (`:5772`) for the SLua IDE bridge. The AFK quit check uses `gAgent.getAFKDuration()`
(`:5825`).

### Main loop and idle step

`doFrame()` (`llappviewer.cpp:1562`) does the following, with each step bracketed by
`pingMainloopTimeout` probes:

1. Discord callbacks (`#ifdef LL_DISCORD`, `:1564`).
2. Native window events and `gatherInput` (`:1645`–`1661`).
3. Posts the canonical `"mainloop"` event (`mainloop.post(newFrame)`, `:1684`), then
   `llcoro::suspend()` so that coroutines run, and rethrows coroutine exceptions (`:1690`).
4. Joystick/keyboard scan (`:1700`–`1714`).
5. `idle()` (`:1742`).
6. `display()` (`:1774`, rendering, chapter 04), then the optional snapshot.
7. Yield/sleep policy (`YieldTime`, `BackgroundYieldTime`, `gPeriodicSlowFrame`,
   `:1808`–`1856`).
8. Texture threads, the LFS I/O thread, `gMeshRepo` and the texture cache are pumped
   with a frame-time budget (`:1867`–`1899`).
9. A frame limiter sleep (`:1911`).

`frame()` (`:1522`) wraps `doFrame()` in `LLContinueError` and `bad_alloc` handlers.

`idle()` (`:5753`) is the integration point for this chapter, in this order:

| Order | Step | Line |
|---|---|---|
| 1 | Frame time, `LLEventTimer::updateClass()`, `LLPerfStats`, toasts, interpolants, `LLMortician`, websocket manager | `:5761`–`:5772` |
| 2 | `LLImageGL::updateClass()`, main-loop work queue `runFor` | `:5781`, `:5795` |
| 3 | **Before `STATE_STARTED`: `idle_startup()`**; returns early while no world exists | `:5841`–`:5845` |
| 4 | Autopilot (`gAgentPilot.updateTarget`), `send_agent_update(false)` | `:5877`, `:5881` |
| 5 | `idleNameCache()`, `idleNetwork()` (message pump), AFK check, `update_statistics()` | `:5943`–`:5952` |
| 6 | `gEventNotifier.update()`, **`gIdleCallbacks.callFunctions()`**, `gInventory.idleNotifyObservers()`, `LLAvatarTracker::instance().idleNotifyObservers()` | `:5968`–`:5972` |
| 7 | `gViewerWindow->updateUI()` (runs even while disconnected) | `:5999` |
| 8 | Region updates during teleport (`LLWorld::updateRegions`, 1 ms budget) | `:6025` |
| 9 | `LLGestureMgr::instance().update()`, `gAgent.updateAgentPosition(...)` | `:6047`, `:6049` |
| 10 | `gObjectList.update(gAgent)`, `GLTFSceneManager::update()` | `:6058`, `:6059` |
| 11 | Dead drawable/object cleanup, `LLSelectMgr::updateEffects()` | `:6070`–`:6095` |
| 12 | `LLWorld::updateVisibilities()`, `updateRegions()`, `gPipeline.updateMove()`, `LLWorld::updateParticles()` | `:6115`–`:6159` |
| 13 | `gAgentCamera.updateCamera()`, media focus, marketplace importer and notifications | `:6176`–`:6184` |
| 14 | `gObjectList.updateApparentAngles()`, `LLAvatarRenderInfoAccountant::idle()` | `:6189`, `:6193` |
| 15 | Audio volume, listener, wind, `gAudiop->idle()` | `:6196`–`:6205` |
| 16 | `idleShutdown()` when quitting | `:6215` |

During startup, `idle_startup()` itself calls `gIdleCallbacks.callFunctions()`
(`llstartup.cpp:695`).

### Startup state machine (LL upstream with Firestorm/OpenSim additions)

`EStartupState` is declared at `llstartup.h:57`–`93`. `class LLStartUp` is at
`llstartup.h:100`, and `LLStartUp::gStartupState` starts as `STATE_FIRST`
(`llstartup.cpp:296`). `idle_startup()` (`llstartup.cpp:657`) is one function of about
2,840 lines (657 to roughly 3,495). Each state is an `if (STATE_X == getStartupState())`
block. 48 `setStartupState(` calls exist in newview. Outside `llstartup.cpp`,
`getStartupState()` appears 98 times in 45 files and `STATE_STARTED` 65 times.

| State | Handler line | What it does (from code and enum comments) |
|---|---|---|
| `STATE_FIRST` | `:760` | Message-system config (`LLMessageConfig::initClass`), first-run and GPU-change checks, log directories, `start_messaging_system`, `gDirUtilp` setup |
| `STATE_FETCH_GRID_INFO` (FS/OpenSim, `<AW: opensim>`) | `:1071` | Downloads the grid list `grids.xml`; Vulkanstorm forces HTTPS and peer verification for the stock feed |
| `STATE_AUDIO_INIT` (FS) | `:1098` | Creates `gAudiop`: Vulkanstorm `LLAudioEngine_VKS` under `LL_VKS_AUDIO`, otherwise FMOD/OpenAL |
| `STATE_BROWSER_INIT` | `:1297` | Initialises the login-screen browser |
| `STATE_LOGIN_SHOW` | `:1309` | Shows the login panel (`login_show()` `:3495`) |
| `STATE_LOGIN_WAIT` | `:1427` | Waits for user input |
| `STATE_LOGIN_CLEANUP` | `:1443` | Credential handling, per-account directories (chat logs, user dir), per-account settings load |
| `STATE_AGENTS_WAIT` | `:1745` | Waits for `agents.xml` |
| `STATE_LOGIN_AUTH_INIT` | `:1763` | Starts the XML-RPC/LLSD login through `LLLoginInstance` |
| `STATE_LOGIN_CURL_UNSTUCK` | `:1810` | Progress update |
| `STATE_LOGIN_PROCESS_RESPONSE` | `:1821` | Parses the reply (`process_login_success_response()` `:4773`), certificate errors, saves credentials, voice authorisation |
| `STATE_LOGIN_CONFIRM_NOTIFICATON` | `:2037` | Waits for login notification confirmation |
| `STATE_WORLD_INIT` | `:2050` | `gViewerWindow->initWorldUI()`, `LLWorld::addRegion(first sim)` `:2136`, `gAgent.setRegion`, `gAgentCamera.init` |
| `STATE_MULTIMEDIA_INIT` / `STATE_FONT_INIT` | `:2175` / `:2184` | `LLStartUp::multimediaInit()` `:4288`, `fontInit()` `:4300` |
| `STATE_SEED_GRANTED_WAIT` | `:2195` | Waits for the seed capability |
| `STATE_SEED_CAP_GRANTED` | `:2269` | `register_viewer_callbacks()` (`:3812`, 116 `setHandlerFunc*` registrations in that function; 123 in the file), sky init, xfer callbacks, region-changed callbacks |
| `STATE_WORLD_WAIT` | `:2514` | Waits for the simulator circuit |
| `STATE_AGENT_SEND` / `STATE_AGENT_WAIT` | `:2529` / `:2587` | `send_complete_agent_movement`, waits for `AgentMovementComplete` |
| `STATE_INVENTORY_SEND` | `:2625` | `gInventory.loadSkeleton` (agent and library), money balance, mute list, agent language |
| `STATE_INVENTORY_SKEL` / `STATE_INVENTORY_SEND2` / `STATE_INVENTORY_CALLBACKS` | `:2682` / `:2714` / `:2851` | Parent/child map, system folders, background fetch, observer registration; RLVa `onIdleStartup` idle callback `:2906` |
| `STATE_MISC` | `:2920` | Throttle/bandwidth (Vulkanstorm: only raises the throttle, `:2931`), script library, camera focus, settings save |
| `STATE_PRECACHE` | `:3176` | Initial outfit, wearables load start |
| `STATE_WEARABLES_WAIT` | `:3241` | Waits for wearables or a timeout |
| `STATE_CLEANUP` | `:3304` | Final UI reveal, IM floater, FS LSL bridge `initBridge`, benefits, places query; sets `STATE_STARTED` `:3386`; `handleLoginComplete()` `:3413` |
| `STATE_STARTED` | — | Steady state; `idle()` stops calling `idle_startup()` |

`startupStateToString()` (`:4176`) and `LLStartupListener` expose the state to automation.
`reset_login()` (`:4239`) and `transition_back_to_login_panel()` (`:5322`) go back to
`STATE_BROWSER_INIT`.

**Grid managers.** These are compiled under mutually exclusive CMake conditions
(`newview/CMakeLists.txt:935`–`945`): `llviewernetwork.cpp` (LL `LLGridManager`) when
`NOT OPENSIM`, and `fsgridhandler.cpp` (FS, 1,493 lines) when `OPENSIM`. The Release
configuration is non-OpenSim (`doc/rust_port.md`, "Required Release configuration"), so
Release ships the LL file. `lfsimfeaturehandler.cpp` (FS) is always compiled.
`LLGridManager::getInstance` appears 375 times in 80 files.

### Settings

**Files** (`indra/newview/app_settings/`). Counts are `grep -c '<key>Comment</key>'`
(equal to the `Type` key count for `settings.xml`).

| File | Group (`settings_files.xml`) | Entries | Notes |
|---|---|---|---|
| `settings.xml` | Global (`gSavedSettings`) | 2,397 | 27,679 lines |
| `settings_per_account.xml` | PerAccount (`gSavedPerAccountSettings`) | 136 | |
| `settings_crash_behavior.xml` | CrashSettings (`gCrashSettings`) | 4 | |
| `ignorable_dialogs.xml` | Warnings (`gWarningSettings`) | 22 | |
| `quick_preferences.xml` | QuickPreferences (FS) | — | optional |
| `settings_firestorm.xml`, `settings_phoenix.xml`, `settings_hybrid.xml`, `settings_text.xml`, `settings_v3.xml` | Session layer (`SessionSettingsFile`, FS "viewer modes") | 24 / 47 / 36 / 62 / 27 | default `settings_firestorm.xml` (`llappviewer.cpp:3185`) |
| `cmd_line.xml` | command-line option map | 53 top-level keys | consumed by `LLCommandLineParser` |

`settings_files.xml` defines the layering Default → User → Account → Session →
UserSession, plus skin colour groups. `loadSettingsFromDirectory()`
(`llappviewer.cpp:2859`) applies them, and the FS session layer is set up at `:3172`–`:3235`.
`LLControlGroup` lives in `indra/llxml/llcontrol.cpp` (chapter 01). It reads and writes
through `LLSDSerialize::fromXML`/`toPrettyXML` (`llcontrol.cpp:1107`, `:1083`), so settings
I/O already passes through the Rust LLSD rung.

**Usage counts** (`indra/newview`).

| Pattern | Count | Command |
|---|---|---|
| `gSavedSettings` | 5,111 occurrences in 437 files | `grep -ow gSavedSettings *.cpp *.h \| wc -l` |
| `gSavedSettings.get*/set*` calls | 3,766 | `cat *.cpp \| grep -cE 'gSavedSettings\.(get\|set)[A-Z]'` |
| Distinct names read/written by literal through those calls | 1,430 | see State commands |
| `LLCachedControl<` | 976 | `grep -o 'LLCachedControl<'` |
| Distinct names in `LLCachedControl<T> x(gSavedSettings, "...")` | 628 | see State commands |
| `gSavedPerAccountSettings` | 504 in 81 files | |
| `gSavedSettings` outside newview | 14 files (mostly `extern` declarations in llui, llrender, llwindow, llprimitive, llplugin, llappearance) | `grep -rlw gSavedSettings --include=*.cpp --include=*.h indra \| grep -v newview` |

**Change listeners.** `settings_setup_listeners()` (`llviewercontrol.cpp:1301`) contains 210
`setting_setup_signal_listener`/`connect` registrations that wire settings to renderer,
UI and network handlers. `llviewercontrol.cpp` has 59 `handle*` callback functions.

**Presets and input.** `LLPresetsManager` (`llpresetsmanager.h:61`) handles graphics and
camera presets. `LLViewerInput` (`llviewerinput.h:78`) binds keys from
`key_bindings.xml`, and `LLKeyConflict` resolves conflicts. `LLViewerJoystick` handles
NDOF. `quickprefs.cpp` (FS, 2,219 lines) is the dynamic quick-preferences floater.

### Agent, camera, movement (LL upstream, heavy FS/RLVa patching)

| Item | Location |
|---|---|
| `class LLAgent : public LLOldEvents::LLObservable` | `llagent.h:93`; `extern LLAgent gAgent` `llagent.h:1141` |
| `LLAgent::init()` / `cleanup()` | `llagent.cpp:556` / `:607` |
| Movement: `moveAt()`, `setFlying()`, `updateAgentPosition()` | `:760`, `:1012`, `:2514` |
| Region binding: `setRegion()` | `:1178` |
| Autopilot: `startAutoPilotGlobal()` | `:2072`; scripted pilot `LLAgentPilot` (`gAgentPilot`, 22 uses) |
| Animation requests | `sendAnimationRequests()` `:3886` |
| Teleport request classes | `LLTeleportRequest` and 4 subclasses, `llagent.cpp:149`–`246` |
| Teleport API | `teleportRequest()` `:5093`, `teleportViaLandmark()` `:5120`, `teleportViaLure()` `:5187`, `teleportViaLocation()` `:5264`, `teleportViaLocationLookAt()` `:5416`, `teleportCancel()` `:5228`, `teleportCore()` `:4780`, `restartFailedTeleportRequest()` `:4875`, `handleTeleportFinished()` `:4952`, `handleTeleportFailed()` `:4997`, `setTeleportState()` `:5537` |
| FS LSL-bridge teleport | `teleportBridgeLocal()` `:4741`, `teleportBridgeGlobal()` `:4757` |
| Teleport states (8) | `ETeleportState` `llagent.h:717`–`724` (NONE, START, REQUESTED, MOVING, START_ARRIVAL, ARRIVING, LOCAL, PENDING) |
| God mode | `requestEnterGodMode()` `:5732` |
| `class LLAgentCamera` | `llagentcamera.h:78`; `extern gAgentCamera` `:489`; `updateCamera()` `llagentcamera.cpp:1293`, `calcCameraPositionTargetGlobal()` `:1914`, `changeCameraToMouselook()` `:2466`, `changeCameraToThirdPerson()` `:2611`, `setFocusGlobal()` `:2864` |
| Agent update to the sim | `send_agent_update()` `llviewermessage.cpp:4178`, called each idle `llappviewer.cpp:5881` |
| Session identity | `gAgentID`, `gAgentSessionID`, `gAgentUsername` (`llagentdata.h:31`–`33`) |
| Maturity/access | `LLAgentAccess` (`llagentaccess.cpp`, unit-tested) |
| Account benefits | `LLAgentBenefits` (`llagentbenefits.cpp`, 409 lines) |
| Gestures | `LLGestureMgr` (`llgesturemgr.cpp`, 1,654 lines), updated each idle `llappviewer.cpp:6047` |

Teleport flow across files: `LLAgent::teleport*` sends the request.
`process_teleport_start` (`llviewermessage.cpp:3455`), `process_teleport_finish` (`:3641`),
`process_agent_movement_complete` (`:3842`), `process_teleport_failed` (`:7731`) and
`process_teleport_local` (`:7826`) move `setTeleportState`. `LLViewerParcelMgr::onTeleportFinished`
(`llviewerparcelmgr.cpp:2822`) and teleport history observe the outcome. RLVa gates the
requests (`RlvActions::canTeleportToLocation` and similar; see [RLVa](#rlva)).
`fsregioncross.cpp` (FS) smooths region-crossing motion.

**Wearables overview** (cluster `appearance`, owned by 03). `LLAgentWearables`
(`llagentwearables.h:50`, `extern gAgentWearables` `:337`, 189 uses) holds the worn set
(`setWearableOutfit()` `llagentwearables.cpp:1101`, `removeWearable()` `:992`).
`LLAppearanceMgr` (`llappearancemgr.h:44`, 5,463 lines) drives the Current Outfit Folder:
`updateCOF()` `:2293`, `updateAppearanceFromCOF()` `:2767`, `wearInventoryCategory()` `:3152`,
and server-side baking `requestServerAppearanceUpdate()` `:4243` /
`serverAppearanceUpdateCoro()` `:4260`. It registers two idle callbacks (`:4955`, `:4956`).
`aoengine.cpp` (FS, 2,648 lines) is the Firestorm animation overrider.

**Threading.** Main thread only. Appearance updates and AIS calls run as coroutines.

### World, regions, parcels, land (LL upstream)

| Item | Location |
|---|---|
| `class LLWorld : public LLSimpleton<LLWorld>` | `llworld.h:63`; `LLWorld::getInstance` 305 uses in 73 files |
| Region lifecycle | `addRegion()` `llworld.cpp:520`, `removeRegion()` `:677`, `disconnectRegions()` `:1560` |
| Lookup | `getRegionFromPosGlobal()` `:754`, `getRegionFromHandle()` `:838`, `resolveRegionGlobal()` `:917` |
| Per-frame | `updateVisibilities()` `:1060`, `updateRegions()` `:1121`, `updateParticles()` `:1203`, `updateWaterObjects()` `:1350`, `setLandFarClip()` `:1297`, `updateAgentOffset()` `:879` |
| `class LLViewerRegion : public LLCapabilityProvider` | `llviewerregion.h:86` (pimpl `LLViewerRegionImpl` `:82`) |
| Capabilities | seed `setSeedCapability()` `llviewerregion.cpp:3597`, `setCapability()` `:3642`, `getCapability()` `:3733`, `setCapabilitiesReceived()` `:3775`; 118 `capabilityNames.append` entries; coroutines `requestBaseCapabilitiesCoro` `:259`, `requestBaseCapabilitiesCompleteCoro` `:413`, `requestSimulatorFeatureCoro` `:547` |
| Region handshake / sim features | `unpackRegionHandshake()` `:3241`; `requestSimulatorFeatures()` `:2560`, `setSimulatorFeatures()` `:2611` |
| Object cache glue (owned by 03) | `loadObjectCache()` `:832`, `saveObjectCache()` `:858`, `cacheFullUpdate()` `:2927`/`:2982`, `requestCacheMisses()` `:3144`, `idleUpdate()` `:1705` |
| `class LLViewerParcelMgr : public LLSingleton<...>` | `llviewerparcelmgr.h:77`; 318 `getInstance` uses in 69 files |
| Parcel messages | `processParcelOverlay()` `:1545`, `processParcelProperties()` `:1585`, `processParcelAccessListReply()` `:2163`, `sendParcelPropertiesUpdate()` `:1387` |
| Parcel queries | `selectParcelAt()` `:452`, `allowAgentBuild()` `:716`/`:743` |
| Region/estate models | `LLRegionInfoModel`, `LLEstateInfoModel` (`llregioninfomodel.cpp` 254, `llestateinfomodel.cpp` 238) |
| World map data | `LLWorldMap` (726), `LLWorldMapMessage` (324), `LLWorldMipmap` (282, unit-tested); FS OpenSim variant `fsworldmapmessage.cpp` |
| Land layer data | `LLVLManager`, `LLViewerLayer`, `LLWind` |

Include fan-in: `llviewerregion.h` 228 `.cpp` files, `llworld.h` 109, `llviewerparcelmgr.h` 87.
`gAgent.getRegion()` appears 451 times.

### Communication logic: chat, IM, groups, friends, moderation

| Component | Key types and locations | Provenance |
|---|---|---|
| IM model and sessions | `LLIMModel` (`llimview.h:64`), `LLIMMgr` (`llimview.h:389`, `extern gIMMgr` `:705`, 137 uses); `LLIMModel::addMessage` `llimview.cpp:1955`, `LLIMMgr::addMessage` `:3468`, `addSession` `:3938` | LL |
| Incoming IM dispatch | `LLIMProcessing::processNewMessage` `llimprocessing.cpp:703` (handler `process_improved_im` registered `llstartup.cpp:3857`); offline messages `requestOfflineMessages` `:2398` | LL + FS + RLVa |
| Nearby chat | `process_chat_from_simulator` `llviewermessage.cpp:2768`; FS chat hub `fsnearbychathub.cpp` (882) | LL / FS |
| Chat logs | `LLLogChat` (`lllogchat.h:82`), `LLChatLogFormatter::format` `lllogchat.cpp:1041`, `LLChatLogParser::parse` `:1099`; background `LLLoadHistoryThread`/`LLDeleteHistoryThread` (`lllogchat.h:49`, `:69`) | LL |
| Conversation log | `LLConversationLog`, `LLConversationModel` | LL |
| Friends / presence | `LLAvatarTracker` (`llcallingcard.h:81`), `addBuddyList` `llcallingcard.cpp:246`, `processOnlineNotification` `:706`, `processChange` `:719`; 164 `instance()` uses | LL |
| Groups | `LLGroupMgr` (`llgroupmgr.h:364`), `processGroupMembersReply` `llgroupmgr.cpp:956`, `processGroupPropertiesReply` `:1064`, `sendGroupMembersRequest` `:1644`; 203 uses | LL |
| Mute/block | `LLMuteList` (`llmutelist.h:74`), `add` `llmutelist.cpp:344`, `isMuted` `:830`; `LLRenderMuteList` `llmutelist.h:229` | LL |
| Avatar actions / properties | `LLAvatarActions` (2,567), `LLAvatarPropertiesProcessor` (878) | LL |
| Speakers / voice-chat participants | `LLSpeakerMgr` (`llspeakers.cpp`), `LLParticipantList`, FS `fsparticipantlist.cpp` | LL / FS |
| Notification persistence | `LLPersistentNotificationStorage`, `LLDoNotDisturbNotificationStorage` | LL |
| Text tools | `LLAutoReplace` (`replaceWord` `llautoreplace.cpp:705`), `LLTranslate` (1,303) | LL |
| Anti-spam | `NACLAntiSpamRegistry::checkQueue` `NACLantispam.cpp:339` | FS (NACL) |
| Contact sets | `LGGContactSets` (`lggcontactsets.cpp`, 1,307) | FS (LGG) |
| Radar | `FSRadar` (`fsradar.h:57`), `updateRadarList` `fsradar.cpp:178` | FS |
| Chat command line | `chatbar_as_cmdline.cpp` (1,878) | FS |
| Keyword alerts | `FSKeywords` (`fskeywords.cpp`) | FS |
| Firestorm data feed (support groups, blocked versions, agents) | `FSData` (`fsdata.cpp`, 1,077) | FS |
| Group mute | `exogroupmutelist.cpp` | FS (Exodus) |

Threading: main thread, except chat-log load and delete, which run on `LLActionThread`
subclasses (`lllogchat.h:33`).

### Scripting features

No in-viewer Lua engine exists in this tree (`ls indra/newview | grep -i lua` matches only
`luau.txt`, and no CMake file references `luau` or `llluamanager`). "SLua" here means
editing server-side Luau scripts.

| Component | Files | Provenance | Notes |
|---|---|---|---|
| Script editor floater / live editor | `llpreviewscript.cpp` (3,652), `llscripteditor.cpp` (708) | LL + FS + Vulkanstorm port (+554/−261 and +457/−90 since base) | LSL and SLua modes; `keywords_lsl_default.xml`, `keywords_lua_default.xml` |
| Syntax/keywords | `llsyntaxid.cpp` (584); `LLSyntaxIdLSL` holds `mLSLKeywords` and `mLuaKeywords` (`llsyntaxid.h:124`, `:147`) | LL, extended by the 26.4 port | Fetched per region through the `LSLSyntax` capability |
| External IDE bridge | `llscripteditorws.cpp/.h` (3,124 + 395) | LL Viewer 26.4 code ported by Vulkanstorm (`c46d2bacc6`) | WebSocket server through `LLWebsocketMgr`, pumped in `idle()` `llappviewer.cpp:5772` |
| Published-object tracking | `llpublishedobjectmgr.cpp/.h` (1,449 + 349) | same | Runtime event aggregation, script error parsing (`RuntimeEventAggregator`, `llpublishedobjectmgr.h:126`) |
| Scripting floater | `llfloaterscripting.cpp/.h` (132 + 52) | same | |
| IDE handshake files | `llscripteditorposixfile.h` (54) | Vulkanstorm (`b5781b2e47`) | Exclusive POSIX create; CI-compiled test |
| Compile queue (mass recompile/reset) | `llcompilequeue.cpp` (1,212) | LL + FS (preprocessor hook) + port | |
| LSL preprocessor | `fslslpreproc.cpp` (1,623) | FS | `boost::wave` C preprocessor (`:103`); `#include` from inventory (`findInventoryByName` `:73`), `lslopt` `:313`, `lslcomp` `:502`, `encode`/`decode` of the preprocessed header `:136`/`:172`; recursion guard FIRE-31718 `:1519` |
| LSL bridge | `fslslbridge.cpp` (1,737) | FS | `FSLSLBridge : LLSingleton, LLVOInventoryListener` (`fslslbridge.h:43`); creates and attaches a scripted prim (`createNewBridge` `:839`, `create_script_inner` `:1191`), two-way messages `lslToViewer` `:151` / `viewerToLSL` `:559`; idle callback `:101`; 3 `LLEventTimer` subclasses |
| Script library | `fsscriptlibrary.cpp`; `scriptlibrary_{aa,ossl,preproc}.xml` | FS | |
| Script limits/debug/recover | `llfloaterscriptlimits.cpp`, `llfloaterscriptdebug.cpp`, `llfloaterscriptrecover.cpp` | LL | |
| Script dialogs | `llscriptfloater.cpp`, `lltoastscript*.cpp` | LL | |

### RLVa

**Size.** 24 files (11 `.cpp`, 13 `.h`), 16,980 lines (`ls rlv* | xargs wc -l`), of which
12,916 are in `.cpp`. Version constants: 3.4.3, compat 2.9 (`rlvdefines.h:32`–`39`).
`ERlvBehaviour` (`rlvdefines.h:104`) has 150 `RLV_BHVR_*` entries.

| File | Lines (.cpp) | Role |
|---|---|---|
| `rlvhandler.cpp` | 4,029 | `RlvHandler` (`rlvhandler.h:31`), `gRlvHandler` (`:296`); `processCommand` `:455`/`:594`, `hasBehaviour` `:226`, `filterChat` `:1263`, `processIMQuery` `:652`, `onIdleStartup` `:1163`, `onLoginComplete` `:1186`, `setEnabled` `:1549` |
| `rlvhelper.cpp` | 2,125 | `RlvCommand` parsing (`RlvCommand::RlvCommand` `:721`, `parseCommand` `:760`), helpers; idle callback `:990` |
| `rlvlocks.cpp` | 1,311 | Attachment, wearable and folder locks (`gRlvAttachmentLocks`, `gRlvWearableLocks`, `RlvFolderLocks`) |
| `rlvcommon.cpp` | 1,023 | `RlvSettings`, `RlvStrings`, `RlvUtil` |
| `rlvfloaters.cpp` | 906 | Status/locks/strings floaters |
| `rlvinventory.cpp` | 873 | `#RLV` shared folder |
| `rlvactions.cpp` | 736 | `RlvActions` (`rlvactions.h:39`), the query facade used by the rest of the viewer |
| `rlvenvironment.cpp` | 730 | Environment (windlight) commands |
| `rlvui.cpp` | 495 | UI enable/disable (`RlvUIEnabler`) |
| `rlveffects.cpp` | 410 | Visual effects (sphere etc.) |
| `rlvextensions.cpp` | 278 | Extension commands |

**Entry points from the viewer.**

- Object commands arrive as `@` owner-say chat:
  `gRlvHandler.processCommand(from_id, strCmd, true)` at `llviewermessage.cpp:3154`.
- IM queries go through `RlvHandler::instance().processIMQuery` at `llimprocessing.cpp:962`.
- Outgoing chat is filtered at `fsnearbychathub.cpp:128` and `llfloaterimnearbychat.cpp:908`.
- Incoming chat is filtered at `llviewermessage.cpp:3004`.
- RLVa starts from `STATE_INVENTORY_CALLBACKS` (`llstartup.cpp:2906`).

**Hook sites outside `rlv*`** (`nonrlv=$(ls *.cpp *.h | grep -v ^rlv)`).

| Pattern | Lines | Files |
|---|---|---|
| `RlvActions::` | 406 | 92 |
| `RLV_BHVR_` | 241 | 78 |
| `gRlvHandler.` | 219 | 64 |
| `rlv_handler_t::isEnabled()` | 134 | 31 |
| `RlvUtil::` | 75 | 25 |
| `RlvStrings::` | 72 | 34 |
| `gRlvAttachmentLocks` | 64 | 20 |
| `gRlvWearableLocks` | 22 | 7 |
| `RlvFolderLocks` | 20 | 3 |
| `RlvSettings::` | 10 | 4 |
| **Any RLVa API** (combined regex) | **907 lines** | **134 files** |
| Files including an `rlv*.h` header | — | 147 |
| `RLVa` comment-tag lines | 1,647 | — (185 files including `rlv*`) |

The combined regex is
`gRlvHandler|RlvActions::|rlv_handler_t::|RlvUtil::|gRlvAttachmentLocks|gRlvWearableLocks|RlvFolderLocks|RlvSettings::|RlvStrings::|RlvInventory::|RlvUIEnabler|RLV_BHVR_`.

Top hook files by combined count: `llviewermenu.cpp` 97, `llviewermessage.cpp` 74,
`llimprocessing.cpp` 38, `llinventorybridge.cpp` 32, `lltoolpie.cpp` 29, `llagent.cpp` 29,
`llavataractions.cpp` 26, `fsnearbychathub.cpp` 22, `llappearancemgr.cpp` 19,
`llpanelobjectinventory.cpp` 18, `fsareasearch.cpp` 15, `lltooldraganddrop.cpp` 14,
`llfloaterworldmap.cpp` 14, `llfloaterimnearbychat.cpp` 14, `llvoavatarself.cpp` 13,
`llpanelpeoplemenus.cpp` 13, `llagentcamera.cpp` 13, `llselectmgr.cpp` 12,
`llgiveinventory.cpp` 12, `llnetmap.cpp` 10.

Subsystems those hooks reach: chat and IM (03/06), menus and tools (05), inventory and
wearables (03), camera and movement (06), teleport and world map (06), selection and
building (03/05), the environment (04), name display everywhere (05). RLVa is the most
cross-cutting module in the tree after the `g*` singletons. It has no tests.

### Pathfinding (LL upstream)

`LLPathfindingManager` (`llpathfindingmanager.h:49`) is a singleton with 31 uses in 9
files. It requests the navmesh per region (`requestGetNavMeshForRegion`
`llpathfindingmanager.cpp:201`), linksets get/set (`:230`, `:271`), characters (`:321`),
agent state (`:358`) and navmesh rebake (`:387`), all over region capabilities. Its data
types (`LLPathfindingNavMesh`, `...NavMeshZone`, `...NavMeshStatus`, `...Linkset[List]`,
`...Character[List]`, `...Object[List]`) are parsed from LLSD. Rendering of the navmesh
is driven from `llfloaterpathfindingconsole.cpp` through `LLPathingLib` (`indra/llphysicsextensionsos/llpathinglib.h:51`; a stub implementation exists in `LLPathingLibStubImpl.h:36`). 16 files and
6,880 lines, none Firestorm-prefixed.

### Experiences, marketplace, economy, search, web services

| Service | Files | Provenance |
|---|---|---|
| Experiences | `llexperiencelog.cpp`, `llfloaterexperience{s,profile,picker}.cpp`, `llpanelexperience*.cpp`, `llpanelgroupexperiences.cpp`; the cache itself is `LLExperienceCache` in `indra/llmessage` (36 uses in 16 newview files) | LL |
| Marketplace | `llmarketplacefunctions.cpp` (1,903): `LLMarketplaceData::initializeSLM` `:754`, `getSLMListings` `:852`, `createSLMListing` `:990`; `llfloatermarketplace*.cpp`, `llpanelmarketplaceinbox*.cpp`; idle queue `:635` | LL |
| Currency and payment | `llcurrencyuimanager.cpp`, `llfloaterbuycurrency*.cpp`, `llbuycurrencyhtml.cpp`, `llfloaterpay.cpp`, `llfloaterbuy*.cpp`, `llproductinforequest.cpp` | LL |
| Avatar picker | `llfloateravatarpicker.cpp` (1,239; UI cluster), capabilities `AvatarPickerSearch`, `GetDisplayNames` | LL |
| Search and directory | `llfloatersearch.cpp` (web), `llfloaterdirectory.cpp` + `llpaneldir*.cpp` (legacy directory; LL 2025 headers), `fsfloatersearch.cpp` (FS, 3,162) | LL / FS |
| Web/social | `llweb.cpp`, `llwebprofile.cpp`, `llflickrconnect.cpp`, `exoflickr*.cpp`, `fsdiscordconnect.cpp`, `fsprimfeed*.cpp` | LL / FS |
| Benefits | `llagentbenefits.cpp` (from the login response; `init_benefits()` `llstartup.cpp:4724`) | LL |

Newview has 109 `getCapability("...")` literal call sites.

### Statistics and diagnostics

`LLViewerStats` (`llviewerstats.cpp`, 1,032) gathers frame stats and runs
`update_statistics()` from `idle()`. `LLViewerStatsRecorder`, `LLPerfStats` (compiled
through `list(APPEND ...)` at `CMakeLists.txt:1789`), `LLAvatarRenderInfoAccountant`
(`idle()` at `llappviewer.cpp:6193`), `LLSceneMonitor`, `LLFastTimerView` and the FS
performance floaters (`fsfloaterperformance.cpp`, `fsfloatervramusage.cpp`) complete the
cluster. Metrics upload: `metricsUpdateRegion()` / `metricsSend()` (`llappviewer.cpp:7075`, `:7087`).

### LLEventAPI listeners

The automation and test surface consists of 19 `LLEventAPI` subclasses
(`grep -hE 'public\s+LLEventAPI' *.h *.cpp | wc -l`), listed in the module map. They
expose the agent, startup state, settings, window, UI, URL dispatcher, inventory,
appearance, gestures, login panel and XML-RPC to event pumps.
`llviewercontrollistener` and `llxmlrpclistener` have tests (only the former is registered).

### Firestorm feature modules

Firestorm and third-party modules by feature. The table covers `fs*` and the other
non-LL prefixes and excludes RLVa. `.cpp` and `.h` line counts are separate. The total is
137 modules: 87,801 `.cpp` lines and 16,990 `.h` lines. Grouping is by filename prefix and feature,
assigned by hand-written regexes (one-off script, not committed).

| Feature | Modules | .cpp lines | .h lines | Modules (names) |
|---|---|---|---|---|
| Poser and avatar alignment | 13 | 10,986 | 3,468 | `animationexplorer`, `fsfloateravataralign`, `fsfloaterposer`, `fsfloaterposestand`, `fsjointpose`, `fsjointrotatetool`, `fsmaniprotatejoint`, `fsmaniptranslatejoint`, `fspose`, `fsposeranimator`, `fsposestate`, `fsposingmotion`, `fsvirtualtrackpad` |
| Chat, IM and nearby chat | 15 | 11,722 | 1,702 | `chatbar_as_cmdline`, `fschathistory`, `fschatoptionsmenu`, `fschatparticipants`, `fsconsoleutils`, `fsfloaterim`, `fsfloaterimcontainer`, `fsfloaternearbychat`, `fskeywords`, `fsnearbychatbarlistener`, `fsnearbychatcontrol`, `fsnearbychathub`, `fsnearbychatvoicemonitor`, `fspanelimcontrolpanel`, `fsparticipantlist` |
| Import/export and local mesh | 9 | 10,057 | 1,121 | `daeexport`, `fsexportperms`, `fsfloaterexport`, `fsfloaterimport`, `fslocalmeshimportbase`, `fslocalmeshimportgltf`, `vjfloaterlocalmesh`, `vjlocalmesh`, `vjlocalmeshimportdae` |
| Build/texture editing | 3 | 7,803 | 1,003 | `fspanelface`, `particleeditor`, `qtoolalign` |
| Login, preferences, quick prefs, UI chrome | 17 | 7,156 | 1,595 | `dialogstack`, `fsdroptarget`, `fsfloaterpartialinventory`, `fsfloaterprotectedfolders`, `fsfloatersplashscreensettings`, `fsfloaterteleporthistory`, `fsfloaterwearablefavorites`, `fspanellogin`, `fspanelpreferenceuisounds`, `fspanelprefs`, `fsscrolllistctrl`, `pieautohide`, `piemenu`, `pieseparator`, `pieslice`, `quickprefs`, `utilitybar` |
| Search, area search, places | 6 | 6,845 | 1,200 | `fsareasearch`, `fsareasearchmenu`, `fsdispatchclassifiedclickthrough`, `fsfloaterplacedetails`, `fsfloatersearch`, `fssearchableui` |
| Contacts, contact sets, groups | 11 | 5,394 | 1,072 | `exogroupmutelist`, `fscontactsfriendsmenu`, `fsfavoritegroups`, `fsfloateraddtocontactset`, `fsfloatercontacts`, `fsfloatercontactsetconfiguration`, `fsfloatergroup`, `fsfloatergrouptitles`, `fsgrouptitleregionmgr`, `fspanelcontactsets`, `lggcontactsets` |
| Animation overrider | 3 | 3,960 | 528 | `ao`, `aoengine`, `aoset` |
| LSL bridge, preprocessor, script library | 5 | 3,605 | 463 | `fslslbridge`, `fslslbridgerequest`, `fslslpreproc`, `fslslpreprocviewer`, `fsscriptlibrary` |
| Grid handling, OpenSim, SLURL | 7 | 3,596 | 855 | `alfloaterregiontracker`, `fsgridhandler`, `fsregioncross`, `fsslurl`, `fsslurlcommand`, `fsworldmapmessage`, `lfsimfeaturehandler` |
| Data feed, utilities, money, misc | 7 | 3,065 | 665 | `fscommon`, `fsdata`, `fsmoneytracker`, `omnifilter`, `omnifilterengine`, `permissionstracker`, `sanitycheck` |
| Radar and people menus | 8 | 2,825 | 632 | `fsavatarsearchmenu`, `fsfloaterradar`, `fsnamelistavatarmenu`, `fspanelradar`, `fsradar`, `fsradarentry`, `fsradarlistctrl`, `fsradarmenu` |
| Social/web connectors | 7 | 2,689 | 587 | `exoflickr`, `exoflickrauth`, `fsdiscordconnect`, `fsfloaterdiscord`, `fsfloaterprimfeed`, `fsprimfeedauth`, `fsprimfeedconnect` |
| Blocking, anti-spam, asset blacklist | 6 | 2,212 | 573 | `NACLantispam`, `fsassetblacklist`, `fsblocklistmenu`, `fsfloaterassetblacklist`, `fsfloaterblocklist`, `fspanelblocklist` |
| Media, voice, streams, sounds | 7 | 2,095 | 682 | `NACLfloaterexploresounds`, `floatermedialists`, `fsfloaterstreamtitle`, `fsfloatervoicecontrols`, `fsfloatervolumecontrols`, `fsfloaterwhitelisthelper`, `streamtitledisplay` |
| Performance, statistics, render settings | 5 | 1,958 | 359 | `fsavatarrenderpersistence`, `fsfloateravatarrendersettings`, `fsfloaterperformance`, `fsfloaterstatistics`, `fsfloatervramusage` |
| Beams/selection colours | 4 | 944 | 225 | `lggbeamcolormapfloater`, `lggbeammapfloater`, `lggbeammaps`, `lggbeamscolors` |
| Notifications (Growl, libnotify) | 4 | 889 | 260 | `desktopnotifierlinux`, `growlmanager`, `growlnotifier`, `growlnotifierwin` |

Firestorm also patches LL files in place: 6,809 `<FS` marker lines across 611 `ll*` files.
That patching is the larger porting hazard, because it is invisible at file level.
Notable examples: the `STATE_FETCH_GRID_INFO`/`STATE_AUDIO_INIT` startup states, the
session-settings layer, the LSL bridge teleport in `LLAgent`, and Discord presence in
`LLAppViewer`.

Not compiled, though present: `fsjointrotatetool.cpp` and `fssearchableui.cpp`
(see [Risks](#risks-and-open-questions)).

### Vulkanstorm additions

No `vks*` source module exists in `indra/newview`. In this chapter's scope, Vulkanstorm
contributes edits plus the SLua port:

| Change | Where | Commit / marker |
|---|---|---|
| Render-backend selection (OpenGL / Mesa Zink; Vulkan maps to OpenGL) | `selectGLBackend()` `llappviewerwin32.cpp:1097`–`1179`, `llappviewerlinux.cpp:104`; `llappviewer.cpp:1200`, `:3730`–`:3758`; preference UI `llfloaterpreference.cpp:4026`–`4094`, `:4235` | `<VulkanStorm>` |
| Graphics identity for settings persistence across driver switches | `llgraphicsidentity.h` (56 lines), `LLFeatureManager::graphicsIdentityChanged()` used at `llstartup.cpp:814`; `LastGraphicsRendererFamily` `:836` | `d12031fb24`; `doc/graphics_settings_persistence.md` |
| Audio engine creation through the Rust audio rung | `llstartup.cpp:46`, `:1122`–`:1123` (`LL_VKS_AUDIO` → `LLAudioEngine_VKS`) | rung six (`doc/rust_port.md` "Rung six") |
| Grid list feed over verified HTTPS | `llstartup.cpp:1037`–`1050` | — |
| Login throttle only raises bandwidth | `llstartup.cpp:2931`–`2945` | — |
| Version-string parser | `llversionparser.h` (51 lines); test `tests/viewer_version_parser_test.cpp` | `734e56aeee`, `71fa20420b` |
| SLua editor and IDE bridge (LL 26.4 code) | `llscripteditorws.*`, `llpublishedobjectmgr.*`, `llfloaterscripting.*`, edits in `llpreviewscript`, `llscripteditor`, `llsyntaxid`, `llcompilequeue`, `llpanelcontents` | `c46d2bacc6` |
| IDE handshake file safety | `llscripteditorposixfile.h`; test `tests/llscripteditorposixfile_test.cpp` | `b5781b2e47` |
| Branding strings | `llappviewer.cpp:2682`, `growlmanager.cpp:212`, `desktopnotifierlinux.cpp:237`, `fspanellogin.cpp:930` | — |
| Idle AFK duration | `llappviewer.cpp:5825` (`gAgent.getAFKDuration()`) | — |

The other Vulkanstorm newview edits belong to other chapters: the texture fetch and cache
port, `llmeshrepository`, `llviewerassetstorage`, `llhttpretrypolicy`, the PPLL alpha path
in `lldrawpoolalpha`/`pipeline`, and inventory edits.

---

## Coupling

### Global variables declared in newview headers

| Measure | Value | Command |
|---|---|---|
| `extern ... g[A-Z]...;` declaration lines in `indra/newview/*.h` | 338 in 77 headers | `grep -E '^\s*extern\s+[^()]*\bg[A-Z][A-Za-z0-9_]*\s*(\[[^]]*\])?\s*;' *.h \| wc -l` |
| Distinct `g[A-Z]` names on non-function `extern` lines | 333 | `grep -hE '^\s*extern\b' *.h \| grep -vE '\(' \| grep -oE '\bg[A-Z]\w*' \| sort -u \| wc -l` |
| Of the 338 lines, shader objects in `llviewershadermgr.h` | 128 | per-header breakdown |
| Then `llviewermenu.h` 46, `llappviewer.h` 36, `llviewerwindow.h` 17, `llviewerdisplay.h` 11, `llviewercontrol.h` 6, `lltoolmgr.h` 6 | | `... \| awk -F: '{print $1}' \| sort \| uniq -c` |

Several of the most-used globals are declared in libraries outside newview:
`gIdleCallbacks` (`llcommon/llcallbacklist.h:78`), `gFloaterView` (`llui/llfloater.h:691`),
`gFocusMgr` (`llui/llfocusmgr.h:156`), `gCacheName` (`llmessage/llcachename.h:152`) and
`gMessageSystem` (llmessage).

### Use counts of the main singletons in newview

Counts are `grep -ow NAME *.cpp *.h | wc -l` (word matches) and the number of files.

| Global | Declared | Uses | Files |
|---|---|---|---|
| `gSavedSettings` | `llviewercontrol.h:54` | 5,111 | 437 |
| `gAgent` | `llagent.h:1141` | 4,074 | 367 |
| `gInventory` | `llinventorymodel.h:789` | 2,244 | 145 |
| `gMessageSystem` | llmessage | 1,062 | 100 |
| `gDirUtilp` | llfilesystem | 896 | 126 |
| `gPipeline` | `pipeline.h:1205` | 877 | 87 |
| `gViewerWindow` | `llviewerwindow.h:579` | 863 | 141 |
| `gAgentAvatarp` | `llvoavatarself.h:466` | 861 | 107 |
| `gAgentCamera` | `llagentcamera.h:489` | 709 | 93 |
| `gSavedPerAccountSettings` | `llviewercontrol.h:55` | 504 | 81 |
| `gObjectList` | `llviewerobjectlist.h:283` | 483 | 115 |
| `gRlvHandler` | `rlvhandler.h:296` | 472 | 73 |
| `gAgentID` | `llagentdata.h:31` | 399 | 117 |
| `gAudiop` | llaudio | 236 | 25 |
| `gFocusMgr` | llui | 200 | 84 |
| `gAgentWearables` | `llagentwearables.h:337` | 189 | 34 |
| `gMenuHolder` | `llviewermenu.h:177` | 181 | 45 |
| `gFloaterView` | llui | 161 | 65 |
| `gMeshRepo` | `llmeshrepository.h:1010` | 139 | 19 |
| `gFloaterTools` | — | 138 | 26 |
| `gIMMgr` | `llimview.h:705` | 137 | 26 |
| `gGLManager` | llrender | 121 | 25 |
| `gIdleCallbacks` | llcommon | 99 | 36 |
| `gTextureList` | `llviewertexturelist.h:307` | 89 | 17 |
| `gSky` | `llsky.h:95` | 89 | 18 |
| `gAgentSessionID` | `llagentdata.h:32` | 85 | 36 |
| `gDisconnected` | `llappviewer.h:450` | 84 | 43 |
| `gDebugInfo` | `llappviewer.h:417` | 81 | 5 |
| `gKeyboard` | llwindow | 80 | 33 |
| `gStatusBar` | `llstatusbar.h:409` | 78 | 24 |
| `gCacheName` | llmessage | 74 | 34 |
| `gFrameTimeSeconds` | `llappviewer.h:433` | 63 | 23 |
| `gAssetStorage` | llmessage | 61 | 28 |

Singletons accessed through `getInstance()`/`instance()` (counted with
`grep -o 'NAME' | wc -l`):

| Accessor | Uses | Files |
|---|---|---|
| `LLFloaterReg::` | 1,548 | 230 |
| `LLNotificationsUtil::` | 1,417 | 240 |
| `LLSelectMgr::getInstance` | 1,040 | 80 |
| `LLGridManager::getInstance` | 375 | 80 |
| `LLViewerParcelMgr::getInstance` | 318 | 69 |
| `LLWorld::getInstance` | 305 | 73 |
| `LLTeleportHistory` (any) | 293 | 16 |
| `LLStartUp::` | 270 | 51 |
| `LLEnvironment::instance` | 249 | 37 |
| `LLAppViewer::instance` | 229 | 67 |
| `LLAppearanceMgr::instance` | 226 | 37 |
| `LLGroupMgr::getInstance` | 203 | 32 |
| `LLMuteList::getInstance` | 183 | 40 |
| `LLTracker::` | 166 | 15 |
| `LLAvatarTracker::instance` | 164 | 44 |
| `LLVoiceClient::getInstance` | 162 | 36 |
| `LLMarketplaceData::instance` | 124 | 10 |
| `LLViewerMedia::getInstance` | 93 | 32 |
| `LLWorldMap::getInstance` | 85 | 15 |

Singleton class counts in newview (`.h` and `.cpp`): `public LLSingleton<` 162,
`LLParamSingleton<` 4, `LLSimpleton<` 6. The `LLSINGLETON(` macro appears 169 times.

Header fan-in across 837 `.cpp` files: `llviewercontrol.h` is included by 435, `llagent.h`
by 419, `llviewerregion.h` by 228, `llappviewer.h` by 110, `llworld.h` by 109,
`llviewerparcelmgr.h` by 87, and `llstartup.h` by 62.

### Idle and deferred-work registration

| Mechanism | Count | Notes |
|---|---|---|
| `gIdleCallbacks.addFunction(...)` | 49 sites in 33 files | 43 `deleteFunction` sites. Heaviest: `llinventorymodelbackgroundfetch.cpp` (13 sites, one callback re-armed), `llinventorygallery.cpp` (4), `llappearancemgr.cpp` (2: `LLAttachmentsMgr::onIdle`, `LLAppearanceMgr::onIdle`), `llinventorypanel.cpp` (2). In this chapter: `fslslbridge.cpp:101`, `rlvhelper.cpp:990`, `llstartup.cpp:2906` (`RlvHandler::onIdleStartup`), `llmarketplacefunctions.cpp:635`, `lldelayedgestureerror.cpp:68`, `fsareasearch.cpp:158`, `fsfloaterexport.cpp:335`, `fsfloaterimport.cpp:107`, `daeexport.cpp:344`, `llfloaterreporter.cpp:161`, `lltoolbrush.cpp:396`. Other chapters: `llviewermedia.cpp:239`, `llvoicevivox.cpp:389`, `llmaterialmgr.cpp:146`, `llaisapi.cpp:808` |
| `doOnIdleOneTime` / `doOnIdleRepeating` | 25 / 7 textual occurrences (29 call sites by regex `doOnIdle(OneTime\|Repeating)\(` in `.cpp`) | `llcommon/llcallbacklist.h:67`, `:70` |
| `LLEventTimer` subclasses | 47 | Timer callbacks driven by `LLEventTimer::updateClass()` in `idle()` (`llappviewer.cpp:5763`) |
| Coroutines `LLCoros::instance().launch` | 136 | Resumed in `doFrame()` through `llcoro::suspend()` |
| `"mainloop"` event pump listeners | 3 `obtain("mainloop")` sites | Posted once per frame (`llappviewer.cpp:1684`) |
| `gMainloopWork` / `postToMainCoro` | 4 / 17 | Thread-to-main hand-off |
| UDP message handlers (`setHandlerFunc*`) | 123 in `llstartup.cpp`, plus 27 elsewhere (`llinventorymodel` 11, `llcallingcard` 6, `llfloaterpay` 3, `llfloatertelehub` 2, `llmutelist` 2, `llviewerparcelmedia` 2, `llfloaterregioninfo` 1) | Dispatched by `idleNetwork()` (`llappviewer.cpp:6521`) |
| Floater registrations | 231 `LLFloaterReg::add(` in `llviewerfloaterreg.cpp` | |
| Menu handlers | 401 `view_listener_t` subclasses, 516 `commit.add`/`enable.add`/`addMenu` registrations in `llviewermenu.cpp` | |
| URL command handlers | 42 `LLCommandHandler` subclasses | `secondlife:///app/...` dispatch |

### The main loop as integration point

All cross-subsystem sequencing happens in two places.

`LLAppViewer::idle()` fixes the order network → callbacks → UI → agent → objects →
cleanup → world → camera → audio, and that order is implicit coupling: for example,
`gAgent.updateAgentPosition` must run before `gObjectList.update`, and dead-object
cleanup must follow `idleUpdate`.

`idle_startup()` fixes the bring-up order of every subsystem and is the only place many
of them are created or registered, including all 123 startup message handlers.

Inference: any Rust replacement of a subsystem below this chapter must keep its call
points in these two functions in the same relative order. The safe pattern is to leave
`idle()`/`idle_startup()` in C++ and swap only the callee behind the C ABI.

---

## Rust port status

Status per `doc/rust_port.md` (rungs done: JPEG 2000, LLSD, prim mesher, image codecs,
message template, HTTP, asset pipeline, audio; textures in progress; inventory is rung
seven on this branch; then object store, renderer, UI, Vulkan) and
`grep -rln 'vks_\|vkstorm_ffi' --include=*.cpp --include=*.h indra`.

| Area in this chapter | Status | Evidence |
|---|---|---|
| Lifecycle, main loop, startup, settings, agent, world, comms, scripting, RLVa, pathfinding, services, FS modules | **C++; no Rust calls** | No `vks_` call in any file of the 06 clusters. The only newview files that include `vkstorm_ffi.h` are `llhttpretrypolicy.cpp`, `llmeshrepository.cpp`, `lltexturecache.cpp`, `lltexturefetch.cpp` and `llviewerassetstorage.cpp` (chapters 02/03) |
| Indirect Rust use | Present | Settings XML load/save go through `LLSDSerialize` (Rust LLSD rung) in `llxml/llcontrol.cpp:1083`, `:1107`. Login and capability HTTP go through the Rust HTTP rung. Audio engine creation at `llstartup.cpp:1123` uses `LLAudioEngine_VKS` (rung six) |
| Oracle copies of app-core code | Present for inventory only | `tests/check_r2_copies.py` checks a frozen copy of the inventory part of `LLAppViewer::disconnectViewer` and of the `llviewergenericmessage.cpp` handlers, held in `tests/llinventorymodel_parity_generic.cpp` |
| Rust crates for any 06 component | None | `doc/rust_port.md` rung list names no application-core rung; the UI rung (`doc/rust_port_ui.md`) is the nearest |

---

## Port assessment

### Must stay in C++ until late (glue)

| Component | Reason |
|---|---|
| `LLAppViewer::init/idle/doFrame/cleanup` | Fixes the order of every subsystem, and owns threads, the work queue, the watchdog and quit sequencing. It should be the last thing ported, or stay as a thin C++ shell |
| `idle_startup()` | 2,840-line state machine. It touches the window, UI, region, agent, inventory, audio, voice, RLVa and FS bridge, and registers 123 message handlers |
| `llviewermenu.cpp` (13,457), `llviewerwindow.cpp` (7,710) | Integration surfaces (05) with 97 RLVa hooks in the menu alone |
| `LLAgent`, `LLAgentCamera` | 4,074 and 709 uses. They read and write region, avatar, object list, camera, parcel, RLVa and settings state each frame |
| RLVa | 907 hook lines in 134 files. It can only move once the subsystems it gates have Rust-side query points, or its `RlvActions` facade can stay C++ over Rust state |
| `LLWorld`, `LLViewerRegion` | Own the region list, circuits, capabilities and the object cache. They depend on the message system (rung three, Rust codec), the object store (planned rung) and the renderer (surface patches, water) |
| Settings access | 5,111 `gSavedSettings` sites and 976 `LLCachedControl` members. The access API must keep its C++ shape. Inference: the store could move to Rust behind `LLControlGroup` (chapter 01) without touching call sites, if `LLCachedControl` signal semantics are kept |

### Leaf logic that could be ported early

These are pure or nearly pure. Each has well-defined inputs and outputs and few or no
globals, and some already have tests that can serve as oracles.

| Candidate | Location | Shape | Existing test/oracle |
|---|---|---|---|
| SLURL parsing/formatting | `LLSLURL` ctors `llslurl.cpp:59`–`385`; FS `fsslurl.cpp` | String → struct | `tests/llslurl_test.cpp` (368 lines, disabled: "RLVa - incompatible", `CMakeLists.txt:3152`) |
| Chat-log line format and parse | `LLChatLogFormatter::format` `lllogchat.cpp:1041`, `LLChatLogParser::parse` `:1099`, `timestamp2LogString` `:371`, `makeLogFileName` `:298`, `cleanFileName` `:359` | LLSD ↔ text line | none; good oracle candidate |
| RLVa command syntax | `RlvCommand::parseCommand` `rlvhelper.cpp:760` | String → (behaviour, option, param) | none |
| Version parsing | `llversionparser.h`; `LLVersionInfo` | String → tuple | `tests/viewer_version_parser_test.cpp` (CI), `tests/llversioninfo_test.cpp` |
| Date formatting | `lldateutil.cpp` | Time → string | `tests/lldateutil_test.cpp` |
| Maturity/access rules | `llagentaccess.cpp` | Flags → bool | `tests/llagentaccess_test.cpp` |
| Help URL building | `llviewerhelputil.cpp` | String building | `tests/llviewerhelputil_test.cpp` |
| World-map mipmap math | `llworldmipmap.cpp` | Integer math | `tests/llworldmipmap_test.cpp` |
| Auto-replace | `LLAutoReplaceSettings::replaceWord` `llautoreplace.cpp:705` | String → string | none |
| Anti-spam rate limiter | `NACLAntiSpamRegistry::checkQueue` `NACLantispam.cpp:339` | Counter state machine | none |
| Keyword alert matching | `fskeywords.cpp` | String match | none |
| Teleport history file format | `LLTeleportHistoryStorage` (`llteleporthistorystorage.h:80`) | LLSD file | none |
| Startup/teleport state enums | `startupStateToString` `llstartup.cpp:4176`; `teleportStateName` `llagent.cpp:5951` | Tables | trivial |
| Pathfinding LLSD models | `llpathfinding{linkset,character,navmeshstatus,object}.cpp` | LLSD → struct | none |
| Command-line parsing | `llcommandlineparser.cpp` (boost::program_options, `cmd_line.xml`) | argv → settings | none |
| Grid info parsing | `fsgridhandler.cpp` / `llviewernetwork.cpp` | XML/LLSD → grid table | `tests/llviewernetwork_test.cpp` |
| LSL preprocessor | `fslslpreproc.cpp` (boost::wave) | Text → text | none. Inference: high value but needs a C-preprocessor crate with matching semantics; byte-parity corpus required |
| Login response parsing | `process_login_success_response` `llstartup.cpp:4773` | LLSD → globals | `tests/lllogininstance_test.cpp` covers the login instance, not this function |

Inference: these leaves are small (most under 1,000 lines) and would not shrink the C++
glue. Porting them early is useful mainly to build oracle corpora and FFI patterns
(string in, LLSD out) that later app-core rungs reuse. It would not move ownership of
any global.

### Prerequisites for moving the core

1. Object store rung (planned) so that `gObjectList`, `LLViewerRegion` object-cache glue
   and `LLWorld` can address Rust-owned objects.
2. A Rust-side settings store, or a stable C ABI over `LLControlGroup`, because every
   component here reads settings.
3. Inventory rung completion (in progress on this branch). Startup states
   `STATE_INVENTORY_*`, appearance, RLVa `#RLV` folders and the LSL bridge all use
   `gInventory`.
4. A decision on RLVa. Either keep `RlvActions` as the C++ query facade over Rust state,
   or give each ported subsystem an RLVa policy hook. The rung order does not cover this.
5. A test harness that can drive `idle_startup()` against a mock grid. The inventory
   mock server `tests/vksinventory_mock_server.py` (992 lines) and asset mock server
   (361 lines) are the starting point.

---

## Existing tests and oracles

Inventory of `indra/newview/tests` (96 entries plus `fixtures/` and `inventory/`;
45,248 lines in `.cpp`/`.h`/`.py`). Registration is from `newview/CMakeLists.txt:3026`–`3266`,
`tests/inventory/r{2,3,4}*.cmake` and `.github/workflows/build.yaml`.

| Category | Tests | Registration | Covers chapter |
|---|---|---|---|
| LL unit tests (`LL_ADD_PROJECT_UNIT_TESTS`) | `llagentaccess_test`, `lldateutil_test`, `lllogininstance_test`, `llviewerhelputil_test`, `llversioninfo_test`, `llworldmap_test`, `llworldmipmap_test` | registered (7) | 06 |
| LL integration tests | `cppfeatures_test`, `llsechandler_basic_test`, `llsecapi_test`, `llviewercontrollistener_test`, `llviewernetwork_test`, `llviewerassetstats_test` | registered (6) | 01/02/06 |
| LL tests present but disabled | `llslurl_test` ("RLVa - incompatible"), `llmediadataclient_test`, `llremoteparcelrequest_test`, `llvocache_test` (commented out), `lltextureinfo_test`, `lltextureinfodetails_test` (`#ADD_VIEWER_BUILD_TEST`), `lltexturestatsuploader_test`, `llwlparammanager_test`, `llxmlrpclistener_test` + `test_llxmlrpc_peer.py` (no registration found) | not run | 02/03/06/07 |
| Stubs | `lldir_stub`, `llglslshader_stub`, `llpipeline_stub`, `llsky_stub`, `llvieweroctree_stub`, `llviewershadermgr_stub`, `llviewertexture_stub`, `llwlanimator_stub`, `llwldaycycle_stub`, `llwlparamset_stub` | support | — |
| Vulkanstorm Rust parity (textures/HTTP) | `lltexturecache_parity_test` (byte parity against the pre-port record `fixtures/lltexturecache_parity.bin`), `lltexturefetch_parity_test` (+ stubs, `vksassets_mock_server.py`, `fixtures/lltexturefetch_parity.xml`, `fixtures/textures/`), `llhttpretrypolicy_test` | registered (3) | 02/03 |
| Vulkanstorm inventory rung (rung seven) | `llinventorymodel_parity` (R2: `_test`, `_harness`, `_stubs`, `_generic`, `_c1`–`_c5`, `_msgs`, `_real`, `_final`), `llinvfetch_parity` (R3 fetch: `_test`, `_ais`, `_bgf`, `_scenarios`, `_support`), `llaisupdate_parity` (R3 update), `llinventory_offer_oracle` (R4 offer: `_test`, `_copies`, `_fakes`, `_frozen`, `_guards`, `_boundary`), `llinventory_task_oracle` (R4 task: `_test`, `_fakes`, `_boundary`, `_decls`); mock server `vksinventory_mock_server.py`; frozen-copy checkers `check_r2_copies.py`, `check_r3a_copies.py`, `check_r3b_copies.py`, `check_r4a_copies.py`, `check_r4b_copies.py` | registered (5) through `tests/inventory/*.cmake` | 03 (and 06 for `disconnectViewer` copy) |
| Vulkanstorm audio | `vksaudio_mock_stream_server.py`, `fixtures/music/` | used by the rung-six tests | 07 |
| Vulkanstorm standalone (CI) | `viewer_version_parser_test.cpp`, `llscripteditorposixfile_test.cpp` | built with `g++` directly in `.github/workflows/build.yaml:36`, `:40` | 06 |

Coverage for this chapter:

- **Tested:** `LLAgentAccess`, `LLDateUtil`, `LLLoginInstance`, `LLViewerHelpUtil`,
  `LLVersionInfo` and the version parser, `LLWorldMap`, `LLWorldMipmap`,
  `LLViewerControlListener`, `LLGridManager` (`llviewernetwork`, non-OpenSim only),
  `LLSecAPI`/`LLSecHandlerBasic`, and the IDE handshake file helper.
- **Untested:** `LLAppViewer`, `idle_startup()`, `LLAgent`, `LLAgentCamera`, `LLWorld`,
  `LLViewerRegion`, `LLViewerParcelMgr`, `LLIMMgr`/`LLIMProcessing`, `LLGroupMgr`,
  `LLMuteList`, `LLAvatarTracker`, `LLLogChat`, all of RLVa, the LSL preprocessor and
  bridge, pathfinding, marketplace, experiences, and every Firestorm module.
- `llslurl_test` exists but is disabled. Re-enabling it is the cheapest gain.

---

## Risks and open questions

| # | Risk / question | Detail |
|---|---|---|
| 1 | **Implicit ordering in `idle()` and `idle_startup()`** | No test drives either. A swapped callee that changes timing or re-entrancy (for example, firing inventory observers from a Rust callback) could break bring-up in ways no current parity test sees |
| 2 | **RLVa reach** | 907 hook lines in 134 files, no tests, and the slurl test is disabled because of RLVa. Each later rung (object store, renderer, UI) must preserve RLVa's gating semantics without an oracle. Open: whether to write RLVa oracle tests before the object-store rung |
| 3 | **In-place Firestorm patches** | 6,809 `<FS` lines in 611 LL files. Frozen oracle copies must capture the Firestorm variant, not the LL one, and the `check_r*_copies.py` approach must extend to app-core code |
| 4 | **Dead sources in the tree** | 10 `.cpp` files (2,619 lines) have no live `CMakeLists.txt` entry: `fsjointrotatetool` 66, `fssearchableui` 153, `llenvadapters` 67, `llfloaterperformance` 755 (commented `:412`), `llfloaterpreferencesgraphicsadvanced` 408 (commented `:418`), `llpanelimcontrolpanel` 47, `llpaneltopinfobar` 522 (commented `:652`, "unused"), `llsrv` 64, `lltoolview` 196, `llwldaycycle` 341. Check: `sed -E 's/#.*$//' CMakeLists.txt > cm.txt` then grep each name. They inflate counts and should not be ported |
| 5 | **OpenSim vs SL code paths** | `fsgridhandler.cpp` and `llviewernetwork.cpp` are alternative sources. `STATE_FETCH_GRID_INFO` is FS/OpenSim. Release is non-OpenSim, but the Rust package refuses OpenSim builds (`doc/rust_port.md`). Open: how OpenSim-only code is tested once its neighbours are Rust |
| 6 | **Settings surface size** | 2,397 global and 136 per-account settings, 1,430 distinct literal names read through `get*/set*`, and 628 through `LLCachedControl`. Inference: some declared settings are unused or only read through UI XML. The surface has not been measured against `skins/` XML; a Rust settings store needs that census |
| 7 | **Global initialisation order** | 338 `extern g*` declarations. Static-storage singletons (`gAgent`, `gInventory`, `gObjectList`, `gPipeline` are objects, not pointers) are constructed before `main`. A Rust owner would have to replace them with pointers or accessors, which touches thousands of `gX.` sites. Open: keep C++ static facades forwarding into Rust? |
| 8 | **Coroutines on the main thread** | 136 coroutine launches across the viewer. Inference: any Rust code called from a coroutine must not block, and Rust async work posting back must use `gMainloopWork`/`postToMainCoro`. No FFI convention for this is documented in `doc/rust_port.md` for app-core rungs |
| 9 | **SLua port provenance** | `llscripteditorws.cpp` and `llpublishedobjectmgr.cpp` are LL 2025 code ported ahead of Firestorm. Future Firestorm merges could conflict, and they have no tests besides the POSIX handshake helper |
| 10 | **Module-map boundaries** | Cluster ownership for `llselectmgr`, `llviewercamera`, `llaisapi`, `llviewerwindow`/`llviewermenu`, login panels and the appearance cluster is a judgement call (see the module map notes). Other chapters may count these files differently. This chapter's totals assume the assignments above |
| 11 | **`ll*` prefix as provenance** | Several `ll*` files are Firestorm-revived or Firestorm-authored. The LL/FS line split from prefixes is approximate. A precise split would need `git blame` against the LL upstream merge base, which this survey did not do |

---

## State commands

All commands run from `indra/newview` unless stated otherwise. Git Bash on Windows.

```sh
# size
ls *.cpp | wc -l; cat *.cpp | wc -l; ls *.h | wc -l; cat *.h | wc -l
ls ll*.cpp | wc -l; cat ll*.cpp | wc -l; ls fs*.cpp | wc -l; cat fs*.cpp | wc -l
ls rlv* | xargs wc -l | tail -1

# fork markers
grep -c -- '<FS' *.cpp *.h | awk -F: '{s+=$2} END{print s}'
grep -l '<FS' ll*.cpp ll*.h | wc -l
grep -ho '<FS:[A-Za-z]*' *.cpp *.h | sort | uniq -c | sort -rn | head

# Vulkanstorm delta (repo root)
git diff --numstat 10bd3c9f930c76e1427ddd4ecece6cdf36b4406d HEAD -- 'indra/newview/*.cpp' 'indra/newview/*.h'
git diff --name-status 10bd3c9f930c76e1427ddd4ecece6cdf36b4406d HEAD -- 'indra/newview/*.cpp' 'indra/newview/*.h' | grep -v '^M'

# globals
grep -E '^\s*extern\s+[^()]*\bg[A-Z][A-Za-z0-9_]*\s*(\[[^]]*\])?\s*;' *.h | wc -l
grep -hE '^\s*extern\b' *.h | grep -vE '\(' | grep -oE '\bg[A-Z][A-Za-z0-9_]*' | sort -u | wc -l
for g in gAgent gSavedSettings gInventory gViewerWindow gObjectList gPipeline; do
  echo "$g $(grep -ow $g *.cpp *.h | wc -l) $(grep -lw $g *.cpp *.h | wc -l)"; done

# settings
cd app_settings; for f in settings*.xml ignorable_dialogs.xml; do echo "$f $(grep -c '<key>Comment</key>' $f)"; done; cd ..
cat *.cpp *.h | grep -oE 'gSavedSettings\.(get|set)[A-Za-z0-9]*\("[A-Za-z0-9_]+"' | sed -E 's/.*\("//; s/"$//' | sort -u | wc -l
cat *.cpp *.h | grep -oE 'LLCachedControl<[^>]+>\s+\w+\(\s*gSavedSettings\s*,\s*"[A-Za-z0-9_]+"' | sed -E 's/.*"([A-Za-z0-9_]+)"/\1/' | sort -u | wc -l

# idle callbacks and integration
grep -c 'gIdleCallbacks.addFunction' *.cpp | awk -F: '{s+=$2} END{print s}'
grep -n 'gIdleCallbacks.addFunction' *.cpp
cat *.cpp | grep -c 'LLCoros::instance().launch'
grep -c 'setHandlerFunc' *.cpp | grep -v ':0'

# RLVa hook sites
nonrlv=$(ls *.cpp *.h | grep -v '^rlv')
pat='gRlvHandler|RlvActions::|rlv_handler_t::|RlvUtil::|gRlvAttachmentLocks|gRlvWearableLocks|RlvFolderLocks|RlvSettings::|RlvStrings::|RlvInventory::|RlvUIEnabler|RLV_BHVR_'
cat $nonrlv | grep -cE "$pat"; grep -lE "$pat" $nonrlv | wc -l
awk '/enum ERlvBehaviour/,/};/' rlvdefines.h | grep -cE '^\s*RLV_BHVR_[A-Z_]+'

# startup states
grep -nE 'if ?\(STATE_[A-Z_]+ == LLStartUp::getStartupState\(\)\)' llstartup.cpp

# Rust touch points (repo root)
grep -rln 'vks_\|vkstorm_ffi' --include=*.cpp --include=*.h indra | grep -v tests
```

Module-map classifier (run as `python -I cluster.py indra/newview table`). The rules are
condensed here; the first match wins, and the order matters:

```python
R = [
 ("rlv", "06", [r"^rlv"]),
 ("scripting", "06", [r"^llpreviewscript$", r"^llscripteditor", r"^llpublishedobjectmgr$", r"^llfloaterscripting$",
   r"^llsyntaxid$", r"^llcompilequeue$", r"^fslslpreproc", r"^fslslbridge", r"^fsscriptlibrary$",
   r"^llfloaterscript(debug|limits|recover)$", r"^llfloatergotoline$", r"^llfloatersearchreplace$",
   r"^llfloatermyscripts$", r"^llscriptfloater$", r"^lltoastscript"]),
 ("pathfinding", "06", [r"pathfinding"]),
 ("listeners", "06", [r"listener$"]),
 ("lifecycle", "06", [r"^llappviewer", r"^llcommandlineparser$", r"^llwindebug$", r"^llversioninfo$", r"^llvelopack$",
   r"^llmachineid$", r"^llhasheduniqueid$", r"^bugsplatattributes$", r"^sanitycheck$"]),
 ("startup", "06", [r"^llstartup$", r"^lllogininstance$", r"^llloginhandler$", r"^llviewernetwork$", r"^fsgridhandler$",
   r"^lfsimfeaturehandler$", r"^llfirstuse$", r"^llviewerhome$"]),
 ("settings", "06", [r"^llviewercontrol$", r"^llpresetsmanager$", r"^llviewerinput$", r"^llkeyconflict$", r"^llviewerjoystick$",
   r"^llfloatersettings(debug|color)$", r"^quickprefs$", r"^llsetkeybinddialog$", r"^llfloater(save|load|delete)prefpreset$",
   r"^llfloaterjoystick$"]),
 ("agent", "06", [r"^llagent(?!wearables)", r"^llfollowcam$", r"^llgesturemgr$", r"^llviewergesture$", r"^lldelayedgestureerror$",
   r"^llanimstatelabels$", r"^llemote$", r"^lltracker$", r"^llfloatercamera", r"^llfloatersavecamerapreset$", r"^llmoveview$",
   r"^llfloaterhoverheight$"]),
 ("appearance", "03", [r"^llagentwearables", r"^llappearancemgr$", r"^llattachmentsmgr$", r"^llviewerwearable$", r"^llwearable",
   r"^llcofwearables$", r"^lloutfit", r"^llpaneloutfit", r"^llpanelwearing$", r"^llpaneleditwearable$", r"^llsidepanelappearance$",
   r"^llpanelappearancetab$", r"^llfilteredwearablelist$", r"^llsaveoutfitcombobtn$", r"^ao(engine|set)?$",
   r"^fsfloaterwearablefavorites$", r"^llscrollingpanelparam", r"^lltoolmorph$", r"^llmorphview$", r"^llviewertexlayer$",
   r"^llfloateravatartextures$"]),
 ("poser", "06", [r"^fs(floater)?pos", r"^fsjoint", r"^fsmanip", r"^fsvirtualtrackpad$", r"^fsfloateravataralign$", r"^animationexplorer$"]),
 ("world", "06", [r"^llworld$", r"^llviewerregion$", r"^llviewerparcelmgr$", r"^llparcelselection$", r"^llregioninfomodel$",
   r"^llestateinfomodel$", r"^llregionposition$", r"^llwind$", r"^llworldmap$", r"^llworldmapmessage$", r"^llworldmipmap$",
   r"^fsworldmapmessage$", r"^llremoteparcelrequest$", r"^llaccountingcostmanager$", r"^fsregioncross$", r"^llvlmanager$",
   r"^llviewerlayer$", r"^llfloater(land|landholdings|regioninfo|buyland|sellland|telehub|auction|banduration|regionrestarting|"
   r"regionrestartschedule|regiondebugconsole|godtools|topobjects|openobject|objectweights)$",
   r"^llpanel(land|landaudio|landmedia|grouplandmoney|openregionsettings)$", r"^lltoolselectland$", r"^lltoolbrush$",
   r"^alfloaterregiontracker$"]),
 ("navigation", "06", [r"^llslurl$", r"^fsslurl", r"^llurldispatcher", r"^llurlfloaterdispatchhandler$", r"^llcommandhandler$",
   r"^llurl$", r"^llurlhistory$", r"^llteleporthistory", r"^lllandmark", r"^lllocationhistory$", r"^llsearchhistory$",
   r"^llpanelteleporthistory$", r"^fsfloaterteleporthistory$", r"^llfloatercreatelandmark$", r"^llpanellandmark",
   r"^llfloaterworldmap$", r"^llworldmapview$", r"^llnetmap$", r"^llfloatermap$", r"^llpanelplace", r"^llfloaterplacedetails$",
   r"^fsfloaterplacedetails$", r"^llplaces", r"^llpanelplaces$", r"^lllocationinputctrl$", r"^llnavigationbar$",
   r"^llfavoritesbar$", r"^llpaneltopinfobar$"]),
 ("comms", "06", ["... 53 explicit names: llimview, llimprocessing, llgroupmgr, llavataractions, llmutelist, llcallingcard, "
   "lllogchat, llconversation{log,model}, llspeakers, fsradar, fsradarentry, fsnearbychat{hub,control,voicemonitor}, "
   "NACLantispam, lggcontactsets, chatbar_as_cmdline, fsdata, fscommon, ... (see the comms file list above)"]),
 ("services", "06", [r"experience", r"marketplace", "... currency/buy/pay, flickr, discord, primfeed, web, search, directory, "
   "classified, llagentbenefits"]),
 ("stats", "06", ["... llviewerstats*, llperfstats, llavatarrender*, llfasttimerview, lldebugview, lag meter, scene monitor/view, "
   "FS performance/VRAM/statistics floaters, debug and test floaters"]),
 ("fsexport", "06", [r"^daeexport$", r"^fsfloaterexport$", r"^fsfloaterimport$", r"^fsexportperms$", r"^vjlocalmesh",
   r"^vjfloaterlocalmesh$", r"^fslocalmeshimport", r"^fsareasearch", r"^lggbeam", r"^omnifilter", r"^particleeditor$", r"^qtoolalign$"]),
 ("net", "02", ["llviewermessage, llviewergenericmessage, lleventpoll, llxmlrpctransaction, llappcorehttp, llhttpretrypolicy, "
   "llviewerthrottle, llviewerasset{storage,upload,stats}, llsrv, llsecapi, llsechandler_basic, llaisapi, bulk/model upload"]),
 ("inventory", "03", [r"inventory", "... folder view model, give inventory, folder/asset types, item/task info, previews"]),
 ("objects", "03", ["llviewerobject*, llvovolume, llvoavatar*, llvocache, llvograss, llvotree, flexible, control avatar, mesh "
   "repository, joints, glTF/material managers, local bitmaps/materials, llselectmgr, model/image/bvh previews, llhudeffect*"]),
 ("textures", "03", ["lltexturefetch, lltexturecache, llviewertexture*, lltextureinfo*, lltexturestats, lltextureview, dynamic textures"]),
 ("render", "04", ["pipeline, lldrawpool*, llspatialpartition, llvieweroctree, llface, lldrawable, shader manager, llviewerdisplay, "
   "reflection/hero probes, llenvironment, sky/water/terrain/surface, HUD text/nametags, particles, feature manager, llviewercamera, "
   "environment floaters"]),
 ("media", "07", ["llviewermedia*, llmediactrl, parcel media/audio, llvieweraudio, llvoice*, media/voice floaters and panels"]),
 ("ui_build", "05", [r"^lltool", r"^llmanip", r"^llpanel(object|face|volume|permissions|contents)$", r"^fspanelface$",
   r"^llfloater(tools|inspect|buildoptions|beacons)$", r"^llviewerattachmenu$", r"^pie"]),
 ("ui_comms", "05", ["IM/chat/people/group/profile floaters and panels, chiclets, avatar/group lists, inspectors, name controls"]),
 ("ui_notify", "05", [r"^lltoast", r"^llnotification", r"^llscreenchannel$", r"^llsyswell", r"^growl", r"^desktopnotifierlinux$",
   r"^dialogstack$", r"^llhints$"]),
 ("ui_shell", "05", [r".*"]),
]
```

The full regex lists of this snapshot were not committed (the script was a one-off;
only this file is written). The comms, services, stats, net, inventory, objects, textures, render, media
and ui_comms entries above summarise explicit name lists. Every file name in each
cluster is listed under [Files per cluster](#files-per-cluster-for-this-chapter-largest-first-cpp-lines)
for this chapter, and in summary for the others.
