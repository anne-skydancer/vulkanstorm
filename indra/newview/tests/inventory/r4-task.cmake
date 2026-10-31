# -*- cmake -*-
# Recorder R4a: object contents (task inventory). Owned by R4a alone.
#
# INTEGRATION_TEST_llinventory_task_oracle drives the task-inventory decision
# code of llviewerobject.cpp and llviewerinventory.cpp through one boundary
# (tests/llinventory_task_oracle_boundary.{h,cpp}); its implementation today is
# the frozen copies in tests/llinventory_task_oracle.cpp, checked against the
# live source by tests/check_r4a_copies.py (run by the test itself, test 8).
# Since the R4a wiring the replay runs against the viewer's own implementation
# (the Rust decisions and lltaskinventoryexecutor.inl, through
# tests/llinventory_task_oracle_rust.cpp); a dump, and a replay with
# VKS_TASK_BOUNDARY=frozen, use the frozen copies.
# Fakes stand in for the viewer singletons; llinventory, the message system
# and template, the xfer manager and LLDir are real. The test replays the
# fixtures in tests/fixtures/inventory/task and, with VKS_TASK_DUMP=<dir> in
# the environment, writes them instead (and then fails, by design).
LL_ADD_INTEGRATION_TEST(llinventory_task_oracle
  "tests/llinventory_task_oracle.cpp;tests/llinventory_task_oracle_boundary.cpp;tests/llinventory_task_oracle_fakes.cpp;tests/llinventory_task_oracle_rust.cpp"
  "llinventory;llmessage;llcorehttp;llfilesystem;llxml;llmath;llcommon"
  )
set_property(TARGET INTEGRATION_TEST_llinventory_task_oracle APPEND PROPERTY
    COMPILE_DEFINITIONS
    "VKS_TASK_FIXTURE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/inventory/task\""
    "VKS_TASK_TEMPLATE_FILE=\"${CMAKE_SOURCE_DIR}/../scripts/messages/message_template.msg\""
    "VKS_TASK_WORK_DIR=\"${CMAKE_CURRENT_BINARY_DIR}/llinventory_task_oracle_work\""
    "VKS_TASK_PYTHON=\"${PYTHON_EXECUTABLE}\""
    "VKS_TASK_COPY_CHECK=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/check_r4a_copies.py\"")
