# -*- cmake -*-

project(llprimitive)

include(00-Common)
include(LLCommon)
include(LLCoreHttp)
include(LLPhysicsExtensions)
include(LLPrimitive)
include(GLM)
include(TinyGLTF)
include(ColladaDom)

set(llprimitive_SOURCE_FILES
    lldaeloader.cpp
    llgltfmaterial.cpp
    llmaterialid.cpp
    llmaterial.cpp
    llmaterialtable.cpp
    llmediaentry.cpp
    llmodel.cpp
    llmodelloader.cpp
    llprimitive.cpp
    llprimtexturelist.cpp
    lltextureanim.cpp
    lltextureentry.cpp
    lltreeparams.cpp
    llvolumemessage.cpp
    material_codes.cpp
    )

set(llprimitive_HEADER_FILES
    CMakeLists.txt
    lldaeloader.h
    llgltfmaterial.h
    llgltfmaterial_templates.h
    legacy_object_types.h
    llmaterial.h
    llmaterialid.h
    llmaterialtable.h
    llmediaentry.h
    llmodel.h
    llmodelloader.h
    llprimitive.h
    llprimtexturelist.h
    lllslconstants.h
    lltextureanim.h
    lltextureentry.h
    lltreeparams.h
    lltree_common.h
    llvolumemessage.h
    material_codes.h
    object_flags.h
    )

list(APPEND llprimitive_SOURCE_FILES ${llprimitive_HEADER_FILES})

add_library (llprimitive ${llprimitive_SOURCE_FILES})
target_include_directories( llprimitive  INTERFACE   ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(llprimitive
        llcommon
        llmath
        llmessage
        llcorehttp
        llxml
        llcharacter
        llrender
        ll::colladadom
        ll::glm
        )

if (HAVOK OR HAVOK_TPV)
    target_link_libraries(llprimitive
        llphysicsextensions_impl
        )
else()
    target_link_libraries(llprimitive
        llphysicsextensionsos
        )
endif ()

if(LINUX)
    # GLIB uses pcre, so we need to keep it for Linux
    target_link_libraries(ll::pcre)
endif(LINUX)

#add unit tests
if (LL_TESTS)
    INCLUDE(LLAddBuildTest)
    SET(llprimitive_TEST_SOURCE_FILES
      llmediaentry.cpp
      llprimitive.cpp
      llgltfmaterial.cpp
      )

    set_property(SOURCE llprimitive.cpp PROPERTY LL_TEST_ADDITIONAL_LIBRARIES llmessage)
    LL_ADD_PROJECT_UNIT_TESTS(llprimitive "${llprimitive_TEST_SOURCE_FILES}")

    # Bit-level parity of mesh asset unpacking against the corpus recorded
    # from the C++ unpackers before their port to Rust (vkstorm-rust).
    set(test_libs llprimitive llmessage llcorehttp llxml llmath llcommon)
    LL_ADD_INTEGRATION_TEST(llmeshunpack_parity "" "${test_libs}")
    set_property(TARGET INTEGRATION_TEST_llmeshunpack_parity APPEND PROPERTY
        COMPILE_DEFINITIONS "VKS_MESH_FIXTURE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures\"")
endif (LL_TESTS)
