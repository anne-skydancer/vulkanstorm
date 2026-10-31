# -*- cmake -*-
# Recorder R3a, the HTTP part: what the coroutine adapter's LLSD handler makes
# of an AIS reply (status x body x content type x size, the AIS depth cap,
# the 64 KB worker threshold). Owned by R3a.
#
# INTEGRATION_TEST_llaisadapter_parity feeds the real handler of
# llcorehttputil.cpp (LLCoreHttpUtil::createLLSDHandler, with AIS's depth
# cap) canned HttpResponses and compares the LLSD it posts, and where the
# parse ran, with tests/fixtures/inventory/ais/adapter; with
# VKS_AISADAPTER_DUMP=<dir> in the environment it writes them instead.
LL_ADD_INTEGRATION_TEST(llaisadapter_parity
  ""
  "llmessage;llcorehttp;llcommon"
  )
set_property(TARGET INTEGRATION_TEST_llaisadapter_parity APPEND PROPERTY
    COMPILE_DEFINITIONS
    "VKS_AISADAPTER_FIXTURE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/inventory/ais/adapter\"")
# The test runs as the target's post-build step: rerun it when the fixtures
# change.
set_property(SOURCE tests/llaisadapter_parity_test.cpp APPEND PROPERTY OBJECT_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/inventory/ais/adapter/expected.llsd")
