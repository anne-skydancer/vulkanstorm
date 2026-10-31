# -*- cmake -*-
# Recorder R3a, the HTTP part: what the coroutine adapter's LLSD handler makes
# of an AIS reply (status x body x content type x size, the AIS depth cap,
# the 64 KB worker threshold). Owned by R3a.
#
# INTEGRATION_TEST_llaisadapter_parity feeds the real handler of
# llcorehttputil.cpp (LLCoreHttpUtil::createLLSDHandler, with AIS's depth
# cap) canned HttpResponses and compares the LLSD it posts, and where the
# parse ran, with tests/fixtures/inventory/ais/adapter; with
# VKS_AISADAPTER_DUMP=<dir> in the environment it writes them instead. Its
# second test runs the same matrix through AIS's own handler (llaisreply.cpp:
# the raw response to Rust), which must reproduce the recording.
LL_ADD_INTEGRATION_TEST(llaisadapter_parity
  "llaisreply.cpp"
  "llmessage;llcorehttp;llmath;llcommon"
  )
set_property(TARGET INTEGRATION_TEST_llaisadapter_parity APPEND PROPERTY
    COMPILE_DEFINITIONS
    "VKS_AISADAPTER_FIXTURE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/inventory/ais/adapter\"")
# The test runs as the target's post-build step: rerun it when the fixtures
# change.
set_property(SOURCE tests/llaisadapter_parity_test.cpp APPEND PROPERTY OBJECT_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/inventory/ais/adapter/expected.llsd")
# llaisreply.cpp includes the viewer's precompiled header, which reaches into
# libraries the test does not link: headers only.
target_include_directories(INTEGRATION_TEST_llaisadapter_parity PRIVATE
    $<TARGET_PROPERTY:llui,INTERFACE_INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:llprimitive,INTERFACE_INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:llwindow,INTERFACE_INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:llrender,INTERFACE_INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:llimage,INTERFACE_INCLUDE_DIRECTORIES>
    )
