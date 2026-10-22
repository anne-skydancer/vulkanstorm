# -*- cmake -*-
# Recorder R3a: the AIS response decision corpus (survey 3, layer A). Owned
# by R3a alone.
#
# INTEGRATION_TEST_llaisupdate_parity compiles the real llaisapi.cpp and
# feeds scripted AIS results to AISAPI::InvokeAISCommandCoro (error branch,
# AISUpdate, doUpdate, callbacks). tests/llaisupdate_parity_stubs.cpp records
# every model call it makes over a scripted skeleton; the frozen copies in
# that file are checked against the live source by tests/check_r3a_copies.py.
# It replays tests/fixtures/inventory/ais/update and, with
# VKS_AISUPDATE_DUMP=<dir> in the environment, writes them instead.
#
# AISUpdate's timers and its per-frame yield are injected without touching
# llaisapi.cpp: llaisupdate_parity_stubs.cpp includes every header
# llaisapi.cpp uses, then renames LLTimer to vks_r3a_LLTimer and
# llcoro::suspendUntilNextFrame to vks_r3a_suspendUntilNextFrame and includes
# llaisapi.cpp verbatim, so only the text of llaisapi.h/.cpp sees the renames;
# both are defined there over an injected clock and frame counter. The rest
# of the executable, and the viewer's own build of llaisapi.cpp, keep the
# real ones. (Source-file compile definitions would not do: they are per
# directory and would reach the viewer target.)
LL_ADD_INTEGRATION_TEST(llaisupdate_parity
  "tests/llaisupdate_parity_stubs.cpp"
  "llinventory;llmessage;llcorehttp;llfilesystem;llxml;llmath;llcommon"
  )
set_property(TARGET INTEGRATION_TEST_llaisupdate_parity APPEND PROPERTY
    COMPILE_DEFINITIONS
    "VKS_AISUPDATE_FIXTURE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/inventory/ais/update\""
    "VKS_AISUPDATE_TEMPLATE_FILE=\"${CMAKE_SOURCE_DIR}/../scripts/messages/message_template.msg\""
    "VKS_AISUPDATE_WORK_DIR=\"${CMAKE_CURRENT_BINARY_DIR}/llaisupdate_parity_work\""
    "VKS_AISUPDATE_PYTHON=\"${PYTHON_EXECUTABLE}\""
    "VKS_AISUPDATE_COPY_CHECK=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/check_r3a_copies.py\"")
# The test runs as the target's post-build step: rebuild (and so rerun) it
# when anything it checks changes, the live sources the drift guard reads
# and the fixture tree included.
set_property(SOURCE tests/llaisupdate_parity_test.cpp APPEND PROPERTY OBJECT_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/llviewerinventory.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/llviewerinventory.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/llinventorymodel.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/llinventorymodel.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_r3a_copies.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_r3a_fingerprints.txt"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_r3a_fence.txt"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/inventory/ais/update/cases/index.txt"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/inventory/ais/update/expected/manifest.txt")
# The viewer headers llaisapi.cpp includes reach into libraries the test
# does not link: headers only.
target_include_directories(INTEGRATION_TEST_llaisupdate_parity PRIVATE
    $<TARGET_PROPERTY:llui,INTERFACE_INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:llprimitive,INTERFACE_INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:llphysicsextensionsos,INTERFACE_INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:llmeshoptimizer,INTERFACE_INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:llcharacter,INTERFACE_INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:llappearance,INTERFACE_INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:llplugin,INTERFACE_INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:llwindow,INTERFACE_INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:lllogin,INTERFACE_INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:llaudio,INTERFACE_INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:llrender,INTERFACE_INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:llimage,INTERFACE_INCLUDE_DIRECTORIES>
    )
