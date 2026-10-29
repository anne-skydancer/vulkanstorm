# -*- cmake -*-
# Recorder R4b: offers and giving. Owned by R4b alone.
#
# INTEGRATION_TEST_llinventory_offer_oracle records and replays the oracle of
# inventory offers and giving (survey 4, the instant message part of set 4 and
# sets 5 to 7): the ImprovedInstantMessage packets of gives, accepts and
# declines, received-offer parsing and LLOfferInfo's LLSD form, the offer
# policy table, the give predicates and folder buckets. The decision code is
# frozen in tests/llinventory_offer_oracle_copies.* (checked against the live
# source by tests/check_r4b_copies.py) and driven with the fakes in
# tests/llinventory_offer_oracle_fakes.*; the packets are built by the real
# message system and pack_instant_message. With VKS_R4B_DUMP=<dir> the test
# writes the fixtures (tools/inventory-fixtures/offer/regenerate.py in
# vkstorm-core-rust copies them into the crate). The test also runs
# tests/check_r4b_copies.py. The decision code is reached only through the
# W6/W7 seam in tests/llinventory_offer_oracle_boundary.h.
LL_ADD_INTEGRATION_TEST(llinventory_offer_oracle
  "tests/llinventory_offer_oracle.cpp;tests/llinventory_offer_oracle_copies.cpp;tests/llinventory_offer_oracle_fakes.cpp;tests/llinventory_offer_oracle_frozen.cpp"
  "llinventory;llmessage;llcorehttp;llfilesystem;llmath;llxml;llcommon"
  )
set_property(TARGET INTEGRATION_TEST_llinventory_offer_oracle APPEND PROPERTY
  COMPILE_DEFINITIONS
  "VKS_R4B_TEMPLATE_FILE=\"${CMAKE_SOURCE_DIR}/../scripts/messages/message_template.msg\""
  "VKS_R4B_FIXTURE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/inventory\""
  "VKS_R4B_PYTHON=\"${PYTHON_EXECUTABLE}\""
  "VKS_R4B_VIEWER_ROOT=\"${CMAKE_SOURCE_DIR}/..\"")
