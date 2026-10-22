# -*- cmake -*-

project(llinventory)

include(00-Common)
include(LLCommon)
include(LLCoreHttp)

set(llinventory_SOURCE_FILES
    llcategory.cpp
    lleconomy.cpp #<FS:Ansariel> OpenSim legacy economy
    llfoldertype.cpp
    llinventory.cpp
    llinventorydefines.cpp
    llinventorysettings.cpp
    llinventorytype.cpp
    lllandmark.cpp
    llnotecard.cpp
    llparcel.cpp
    llpermissions.cpp
    llsaleinfo.cpp
    llsettingsbase.cpp
    llsettingsdaycycle.cpp
    llsettingssky.cpp
    llsettingswater.cpp
    lltransactionflags.cpp
    lluserrelations.cpp
    )
    
set(llinventory_HEADER_FILES
    CMakeLists.txt

    llcategory.h
    lleconomy.h #<FS:Ansariel> OpenSim legacy economy
    llfoldertype.h
    llinventory.h
    llinventorydefines.h
    llinventorysettings.h
    llinventorytype.h
    llinvtranslationbrdg.h
    lllandmark.h
    llnotecard.h
    llparcel.h
    llparcelflags.h
    llpermissions.h
    llpermissionsflags.h
    llsaleinfo.h
    llsettingsbase.h
    llsettingsdaycycle.h
    llsettingssky.h
    llsettingswater.h
    lltransactionflags.h
    lltransactiontypes.h
    lluserrelations.h
    )

list(APPEND llinventory_SOURCE_FILES ${llinventory_HEADER_FILES})

add_library (llinventory ${llinventory_SOURCE_FILES})

target_link_libraries( llinventory llcommon llmath llmessage llxml )
target_include_directories( llinventory  INTERFACE   ${CMAKE_CURRENT_SOURCE_DIR})

#add unit tests
if (LL_TESTS)
    INCLUDE(LLAddBuildTest)
    SET(llinventory_TEST_SOURCE_FILES
      # no real unit tests yet!
      )
    LL_ADD_PROJECT_UNIT_TESTS(llinventory "${llinventory_TEST_SOURCE_FILES}")

    #set(TEST_DEBUG on)
    set(test_libs llinventory llmath llcorehttp llfilesystem )
    LL_ADD_INTEGRATION_TEST(inventorymisc "" "${test_libs}")
    LL_ADD_INTEGRATION_TEST(llinventory_runtime "" "${test_libs}")
    LL_ADD_INTEGRATION_TEST(llparcel "" "${test_libs}")

    # vkstorm-rust rung seven (inventory), recorder R1: the data model against
    # its oracle (tests/llinventory_oracle.cpp, frozen copies of the C++ before
    # the port) in process, and against the recorded fixtures (tests/fixtures).
    set(llinventory_parity_defs
        "VKS_MSG_TEMPLATE_FILE=\"${CMAKE_SOURCE_DIR}/../scripts/messages/message_template.msg\""
        "VKS_INV_FIXTURE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures\"")
    LL_ADD_INTEGRATION_TEST(llinventory_parity "tests/llinventory_oracle.cpp;tests/llinventory_tool.cpp" "${test_libs};llmessage;ll::zlib-ng")
    set_property(TARGET INTEGRATION_TEST_llinventory_parity APPEND PROPERTY
        COMPILE_DEFINITIONS ${llinventory_parity_defs} "VKS_INV_NO_MAIN=1")
    # The frozen copies must match the sources they were copied from: the check runs
    # after the test, and every copied source is a dependency of the oracle, so a change
    # to any of them (llviewerinventory.cpp included) rebuilds the test and re-runs it.
    set_property(SOURCE tests/llinventory_oracle.cpp APPEND PROPERTY OBJECT_DEPENDS
        ${CMAKE_SOURCE_DIR}/llcommon/llassettype.h ${CMAKE_SOURCE_DIR}/llcommon/llassettype.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/llpermissionsflags.h
        ${CMAKE_CURRENT_SOURCE_DIR}/llinventorytype.h ${CMAKE_CURRENT_SOURCE_DIR}/llinventorytype.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/llfoldertype.h ${CMAKE_CURRENT_SOURCE_DIR}/llfoldertype.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/llinventorydefines.h ${CMAKE_CURRENT_SOURCE_DIR}/llinventorydefines.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/llpermissions.h ${CMAKE_CURRENT_SOURCE_DIR}/llpermissions.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/llsaleinfo.h ${CMAKE_CURRENT_SOURCE_DIR}/llsaleinfo.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/llinventory.h ${CMAKE_CURRENT_SOURCE_DIR}/llinventory.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/llcategory.h ${CMAKE_CURRENT_SOURCE_DIR}/llcategory.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/llinventorysettings.h ${CMAKE_CURRENT_SOURCE_DIR}/llinventorysettings.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/lltransactionflags.h ${CMAKE_CURRENT_SOURCE_DIR}/lltransactionflags.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/llnotecard.h ${CMAKE_CURRENT_SOURCE_DIR}/llnotecard.cpp
        ${CMAKE_SOURCE_DIR}/newview/llviewerinventory.h ${CMAKE_SOURCE_DIR}/newview/llviewerinventory.cpp)
    add_custom_command(TARGET INTEGRATION_TEST_llinventory_parity POST_BUILD
        COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_r1_copies.py ${CMAKE_SOURCE_DIR}/..
        COMMENT "Checking the frozen copies of the inventory oracle against their sources")
    # Fixture writer and C++ baseline benchmark for the same corpora; built, not run.
    add_executable(llinventory_tool tests/llinventory_tool.cpp tests/llinventory_oracle.cpp)
    target_link_libraries(llinventory_tool ${test_libs} llmessage ll::zlib-ng)
    target_compile_definitions(llinventory_tool PRIVATE ${llinventory_parity_defs})
    set_target_properties(llinventory_tool PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${EXE_STAGING_DIR}")
    if (WINDOWS)
      set_target_properties(llinventory_tool PROPERTIES LINK_FLAGS "/debug /NODEFAULTLIB:LIBCMT /SUBSYSTEM:CONSOLE")
    endif ()
endif (LL_TESTS)
