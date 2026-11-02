# -*- cmake -*-
# Inventory rung (vkstorm-rust rung seven), recorder R2: the inventory model
# and its cache. Owned by R2 alone. Included from the LL_TESTS block of
# indra/newview/CMakeLists.txt.
#
# INTEGRATION_TEST_llinventorymodel_parity compiles the real
# llinventorymodel.cpp, llviewerinventory.cpp and llinventoryobserver.cpp
# against the stubbed viewer environment in tests/llinventorymodel_parity_stubs.*
# (every outward call recorded through one interface), on the real llinventory,
# llmessage (with the real message template), llcommon and LLDir. It replays
# the fixtures in tests/fixtures/inventory/cache and tests/fixtures/inventory/msgs
# (cache bytes, login load and discard cases, mutation sequences with every
# notification batch and outbound message, validation, and the inventory UDP
# message handlers) and, with VKS_INVMODEL_DUMP=<dir> in the environment, writes
# them instead. VKS_INVMODEL_BENCH=<runs> times the cache and validation paths,
# and the cache codec against its C++ before the port
# (tests/llinventorymodel_parity_frozen.cpp).
LL_ADD_INTEGRATION_TEST(llinventorymodel_parity
  "llinventorymodel.cpp;llviewerinventory.cpp;llinventoryobserver.cpp;llviewerfoldertype.cpp;tests/llinventorymodel_parity_stubs.cpp;tests/llinventorymodel_parity_harness.cpp;tests/llinventorymodel_parity_generic.cpp;tests/llinventorymodel_parity_frozen.cpp"
  "llinventory;llmessage;llcorehttp;llfilesystem;llxml;llmath;llwindow;llcommon"
  "-l${ARCH_PREBUILT_DIRS_RELEASE}"
  )
set_property(TARGET INTEGRATION_TEST_llinventorymodel_parity APPEND PROPERTY
    COMPILE_DEFINITIONS
    "VKS_INVMODEL_FIXTURE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/inventory\""
    "VKS_INVMODEL_LIBRARY_DIR=\"${CMAKE_SOURCE_DIR}/llinventory/tests/fixtures/library\""
    "VKS_INVMODEL_TEMPLATE_FILE=\"${CMAKE_SOURCE_DIR}/../scripts/messages/message_template.msg\""
    "VKS_INVMODEL_WORK_DIR=\"${CMAKE_CURRENT_BINARY_DIR}/llinventorymodel_parity_work\""
    "VKS_INVMODEL_PYTHON=\"${PYTHON_EXECUTABLE}\""
    "VKS_INVMODEL_SOURCE_ROOT=\"${CMAKE_SOURCE_DIR}/..\"")
# The viewer headers the three sources include reach into libraries the test
# does not link: headers only.
target_include_directories(INTEGRATION_TEST_llinventorymodel_parity PRIVATE
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
