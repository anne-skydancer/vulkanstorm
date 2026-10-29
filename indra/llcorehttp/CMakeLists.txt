# -*- cmake -*-

project(llcorehttp)

include(00-Common)
include(CURL)
include(OpenSSL)
include(NGHTTP2)
include(ZLIBNG)
include(LLCoreHttp)
include(LLAddBuildTest)
include(LLCommon)
include(Tut)
include(bugsplat)
include(websocketpp)

set(llcorehttp_SOURCE_FILES
    bufferarray.cpp
    bufferstream.cpp
    httpcommon.cpp
    llhttpconstants.cpp
    httpheaders.cpp
    httpoptions.cpp
    httprequest.cpp
    httpresponse.cpp
    httpstats.cpp
    llwebsocketmgr.cpp
    lljsonrpcws.cpp
    _refcounted.cpp
    )

set(llcorehttp_HEADER_FILES
    CMakeLists.txt

    bufferarray.h
    bufferstream.h
    httpcommon.h
    llhttpconstants.h
    httphandler.h
    httpheaders.h
    httpoptions.h
    httprequest.h
    httpresponse.h
    httpstats.h
    llwebsocketmgr.h
    lljsonrpcws.h
    _httpinternal.h
    _httpservice.h
    _mutex.h
    _refcounted.h
    _thread.h
    )

if (DARWIN OR LINUX)
  # Boost headers define unused members in condition_variable so...
  set_source_files_properties(${llcorehttp_SOURCE_FILES}
                              PROPERTIES COMPILE_FLAGS -Wno-unused-variable)
endif (DARWIN OR LINUX)

list(APPEND llcorehttp_SOURCE_FILES ${llcorehttp_HEADER_FILES})

add_library (llcorehttp ${llcorehttp_SOURCE_FILES})
# The HTTP service (request and reply queues, policy, retries, the libcurl
# transport) is Rust (vkstorm-http through vkstorm_ffi); see doc/rust_port.md.
target_link_libraries(
        llcorehttp
        llmath
        llcommon
        ll::libcurl
        ll::openssl
        ll::nghttp2
        ll::websocketpp
        vkstorm_ffi
  )
target_include_directories( llcorehttp INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
# llmessage depends on llcorehttp, yet llcorehttp also depends on llmessage (at least for includes).
# Cannot/Should not use target_link_libraries here to add llmessage to the dependencies, as that would
# lead to circular dependencies (or in case of cmake, the first project declaring it's dependencies wins)
target_include_directories( llcorehttp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../llmessage)

# tests
set(LLCOREHTTP_TESTS ON CACHE BOOL
    "Build and run llcorehttp integration tests specifically")
if (LL_TESTS AND LLCOREHTTP_TESTS)
  SET(llcorehttp_TEST_SOURCE_FILES
      )

  set(llcorehttp_TEST_HEADER_FILES
      tests/test_httpstatus.hpp
      tests/test_refcounted.hpp
      tests/test_httprequest.hpp
      tests/test_httpheaders.hpp
      tests/test_bufferarray.hpp
      tests/test_bufferstream.hpp
      )

  list(APPEND llcorehttp_TEST_SOURCE_FILES ${llcorehttp_TEST_HEADER_FILES})

  # LL_ADD_PROJECT_UNIT_TESTS(llcorehttp "${llcorehttp_TEST_SOURCE_FILES}")

  #    set(TEST_DEBUG on)
  set(test_libs
          llcorehttp
          llmessage
          llmath
        llcommon
      )

  # If http_proxy is in the current environment (e.g. to fetch s3-proxy
  # autobuild packages), suppress it for this integration test: it screws up
  # the tests.
  LL_ADD_INTEGRATION_TEST(llcorehttp
                          "${llcorehttp_TEST_SOURCE_FILES}"
                          "${test_libs}"
                          "-Dhttp_proxy"
                          ${PYTHON_EXECUTABLE}
                          "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_llcorehttp_peer.py"
                          )

  LL_ADD_INTEGRATION_TEST(jsonrpcws "" "${test_libs}")
  LL_ADD_INTEGRATION_TEST(wsserver "" "${test_libs}")

  # Behavioural parity of the HTTP layer against the scenarios recorded from
  # the C++ implementation before its port to Rust (vkstorm-rust), driven
  # against the local mock server in tests/vkshttp_mock_server.py. If
  # http_proxy is in the environment, suppress it: it breaks the loopback
  # exchange.
  LL_ADD_INTEGRATION_TEST(llcorehttp_parity
                          ""
                          "${test_libs}"
                          "-Dhttp_proxy"
                          ${PYTHON_EXECUTABLE}
                          "${CMAKE_CURRENT_SOURCE_DIR}/tests/vkshttp_mock_server.py"
                          )
  set_property(TARGET INTEGRATION_TEST_llcorehttp_parity APPEND PROPERTY
      COMPILE_DEFINITIONS "VKS_HTTP_FIXTURE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures\"")

  #
  # Example Programs
  #
  SET(llcorehttp_EXAMPLE_SOURCE_FILES
      examples/http_texture_load.cpp
      )

  set(example_libs
          llcorehttp
          llmessage
          llmath
        llcommon
      )

  add_executable(http_texture_load
                 ${llcorehttp_EXAMPLE_SOURCE_FILES}
                 )
  set_target_properties(http_texture_load
                        PROPERTIES
                        RUNTIME_OUTPUT_DIRECTORY "${EXE_STAGING_DIR}"
                        )

  if (WINDOWS)
    # The following come from LLAddBuildTest.cmake's INTEGRATION_TEST_xxxx target.
    set_target_properties(http_texture_load
                          PROPERTIES
                          LINK_FLAGS "/debug /NODEFAULTLIB:LIBCMT /SUBSYSTEM:CONSOLE"
                          LINK_FLAGS_DEBUG "/NODEFAULTLIB:\"LIBCMT;LIBCMTD;MSVCRT\" /INCREMENTAL:NO"
                          LINK_FLAGS_RELEASE ""
                          )
  endif (WINDOWS)

  target_link_libraries(http_texture_load ${example_libs})

endif (LL_TESTS AND LLCOREHTTP_TESTS)
