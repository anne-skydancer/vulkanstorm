# -*- cmake -*-
# Inventory rung (vkstorm-rust rung seven), recorder R3: inventory fetch and
# AIS, split between two recorders, each owning its own fragment. Included
# from the LL_TESTS block of indra/newview/CMakeLists.txt.
include(${CMAKE_CURRENT_LIST_DIR}/r3-update.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/r3-adapter.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/r3-fetch.cmake)
