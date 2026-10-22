# -*- cmake -*-
# Recorder R4a: object contents (task inventory). Owned by R4a alone.
#
# INTEGRATION_TEST_llinventory_task_oracle drives frozen copies of the
# task-inventory decision code of llviewerobject.cpp and llviewerinventory.cpp
# (tests/llinventory_task_oracle.cpp, checked against the live source by
# tests/check_r4a_copies.py) with fakes for the viewer singletons, on the real
# llinventory readers, the real message system and template, the real xfer
# manager and LLDir. It replays the fixtures in tests/fixtures/inventory/task
# and, with VKS_TASK_DUMP=<dir> in the environment, writes them instead.
LL_ADD_INTEGRATION_TEST(llinventory_task_oracle
  "tests/llinventory_task_oracle.cpp;tests/llinventory_task_oracle_fakes.cpp"
  "llinventory;llmessage;llcorehttp;llfilesystem;llxml;llmath;llcommon"
  )
set_property(TARGET INTEGRATION_TEST_llinventory_task_oracle APPEND PROPERTY
    COMPILE_DEFINITIONS
    "VKS_TASK_FIXTURE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/inventory/task\""
    "VKS_TASK_TEMPLATE_FILE=\"${CMAKE_SOURCE_DIR}/../scripts/messages/message_template.msg\""
    "VKS_TASK_WORK_DIR=\"${CMAKE_CURRENT_BINARY_DIR}/llinventory_task_oracle_work\"")
