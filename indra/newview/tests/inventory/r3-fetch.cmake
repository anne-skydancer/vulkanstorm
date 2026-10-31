# -*- cmake -*-
# Recorder R3b: the fetch wire scenarios against the mock AIS/caps server
# (survey 3, layer B). Owned by R3b alone. Included from r3.cmake in the
# LL_TESTS block of indra/newview/CMakeLists.txt.
#
# INTEGRATION_TEST_llinvfetch_parity compiles the real llaisapi.cpp (through
# tests/llinvfetch_parity_ais.cpp: LLCoros renamed in its text, the scripted
# shutdown), llinventorymodelbackgroundfetch.cpp (through
# tests/llinvfetch_parity_bgf.cpp: LLTimer renamed, the scripted frame-budget
# clock), llinventorymodel.cpp,
# llviewerinventory.cpp and llinventoryobserver.cpp on R2's stubbed viewer
# environment (tests/llinventorymodel_parity_stubs.*, harness, generic; used
# read-only, with VKS_INVMODEL_STUB_FETCH=0 so the real AIS and background
# fetch code is linked instead of R2's stubs), the real coroutines,
# coprocedure pool and llcorehttp. The test runs under
# tests/vksinventory_mock_server.py, a loopback AIS / legacy-caps server that
# logs every request; it drives the fetch scenarios of survey 3 section 8.2
# and compares the request log, the side-effect journal, the scheduler
# state and the final model with tests/fixtures/inventory/ais/fetch (test 2),
# after checking the frozen copies with tests/check_r3b_copies.py (test 1).
# With VKS_INVFETCH_DUMP=<dir> it writes the fixtures instead (and fails, as a
# dump is not a replay).
# The Second Life fallback scenarios (rule PRE_D2_SL_FALLBACKS in the README).
# The fallbacks are kept on Second Life (the planned D2 removal was dropped),
# so this stays ON; OFF skips the six by name and keeps their fixtures.
option(VKS_INVFETCH_PRE_D2_GATES "llinvfetch_parity: replay the pre-D2 Second Life fallback scenarios" ON)
if (VKS_INVFETCH_PRE_D2_GATES)
  set(VKS_INVFETCH_SKIP_PRE_D2 0)
else ()
  set(VKS_INVFETCH_SKIP_PRE_D2 1)
endif ()
LL_ADD_INTEGRATION_TEST(llinvfetch_parity
  "tests/llinvfetch_parity_ais.cpp;llaisreply.cpp;tests/llinvfetch_parity_bgf.cpp;llinventorymodel.cpp;llviewerinventory.cpp;llinventoryobserver.cpp;llviewerfoldertype.cpp;tests/llinventorymodel_parity_stubs.cpp;tests/llinventorymodel_parity_harness.cpp;tests/llinventorymodel_parity_generic.cpp;tests/llinvfetch_parity_support.cpp;tests/llinvfetch_parity_scenarios.cpp"
  "llinventory;llmessage;llcorehttp;llfilesystem;llxml;llmath;llwindow;llcommon"
  "-l${ARCH_PREBUILT_DIRS_RELEASE}"
  "-Dhttp_proxy"
  ${PYTHON_EXECUTABLE}
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/vksinventory_mock_server.py"
  )
set_property(TARGET INTEGRATION_TEST_llinvfetch_parity APPEND PROPERTY
    COMPILE_DEFINITIONS
    "VKS_INVMODEL_STUB_FETCH=0"
    "VKS_INVFETCH_SKIP_PRE_D2=${VKS_INVFETCH_SKIP_PRE_D2}"
    "VKS_INVFETCH_FIXTURE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/inventory/ais/fetch\""
    "VKS_INVFETCH_TEMPLATE_FILE=\"${CMAKE_SOURCE_DIR}/../scripts/messages/message_template.msg\""
    "VKS_INVFETCH_WORK_DIR=\"${CMAKE_CURRENT_BINARY_DIR}/llinvfetch_parity_work\""
    "VKS_INVFETCH_SOURCE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\""
    "VKS_INVFETCH_PYTHON=\"${PYTHON_EXECUTABLE}\"")
# The viewer headers the sources include reach into libraries the test does
# not link: headers only (as R2's r2.cmake).
target_include_directories(INTEGRATION_TEST_llinvfetch_parity PRIVATE
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
