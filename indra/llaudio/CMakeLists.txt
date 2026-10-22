# -*- cmake -*-

project(llaudio)

include(00-Common)
include(LLAudio)
include(FMODSTUDIO)
include(OPENAL)
include(VKSAudio)
include(Rust)
include(LLCommon)
include(LLWindow)

set(llaudio_SOURCE_FILES
    llaudioengine.cpp
    lllistener.cpp
    llaudiodecodemgr.cpp
    llvorbisencode.cpp
    )

set(llaudio_HEADER_FILES
    CMakeLists.txt

    llaudioengine.h
    lllistener.h
    llaudiodecodemgr.h
    llvorbisencode.h
    llwindgen.h
    )

if (TARGET ll::fmodstudio)
    list(APPEND llaudio_SOURCE_FILES
         llaudioengine_fmodstudio.cpp
         lllistener_fmodstudio.cpp
         llstreamingaudio_fmodstudio.cpp
         )

    list(APPEND llaudio_HEADER_FILES
         llaudioengine_fmodstudio.h
         lllistener_fmodstudio.h
         llstreamingaudio_fmodstudio.h
         )
endif ()

if (TARGET ll::openal)
  list(APPEND llaudio_SOURCE_FILES
    llaudioengine_openal.cpp
    lllistener_openal.cpp
    )
  
  list(APPEND llaudio_HEADER_FILES
    llaudioengine_openal.h
    lllistener_openal.h
    )
endif ()

if (TARGET ll::vksaudio)
    list(APPEND llaudio_SOURCE_FILES llaudioengine_vks.cpp lllistener_vks.cpp llstreamingaudio_vks.cpp)
    list(APPEND llaudio_HEADER_FILES llaudioengine_vks.h lllistener_vks.h llstreamingaudio_vks.h)
endif ()

list(APPEND llaudio_SOURCE_FILES ${llaudio_HEADER_FILES})

add_library (llaudio ${llaudio_SOURCE_FILES})
target_include_directories( llaudio  INTERFACE   ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries( llaudio
        llcommon
        llmath
        llmessage
        llfilesystem
        # The sound asset decoder is Rust (vkstorm-audio); libvorbis stays
        # for LLVorbisEncode (uploads) and as the parity test's oracle.
        vkstorm_ffi
        ll::vorbis
    )

if( TARGET ll::openal )
    target_link_libraries( llaudio ll::openal )
endif()
if( TARGET ll::fmodstudio )
    target_link_libraries( llaudio ll::fmodstudio )
endif()
if( TARGET ll::vksaudio )
    target_link_libraries( llaudio ll::vksaudio )
endif()

if (LL_TESTS AND TARGET ll::vksaudio)
    include(LLAddBuildTest)
    # Parity of the engine's mixed output and of the sound asset decoder
    # against the corpora recorded from the C++ (SoLoud) implementation
    # before its port to Rust (vkstorm-rust rung six). The adapter sources
    # are the only engine-specific parts of the test.
    LL_ADD_INTEGRATION_TEST(llaudio_parity
        "tests/llaudio_parity_vks_adapter.cpp;tests/llaudio_parity_decode_adapter.cpp"
        "llaudio;llfilesystem;llmessage;llmath;llcommon;ll::vorbis;ll::zlib-ng")
    set_property(TARGET INTEGRATION_TEST_llaudio_parity APPEND PROPERTY
        COMPILE_DEFINITIONS "VKS_AUDIO_FIXTURE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures\"")

    # Parcel music end to end through LLStreamingAudio_VKS on the null
    # device, against the local radio server (it runs the test and passes
    # its port in LL_TEST_PORT). The FFmpeg libraries are staged next to the
    # test on Windows; on Linux they are in the prebuilt library directory.
    if (LINUX)
        set(vks_music_test_env "-DVKS_FFMPEG_DIR=${ARCH_PREBUILT_DIRS_RELEASE}")
    else ()
        set(vks_music_test_env "")
    endif ()
    LL_ADD_INTEGRATION_TEST(llaudio_music
        ""
        "llaudio;llfilesystem;llmessage;llmath;llcommon"
        ${vks_music_test_env}
        ${PYTHON_EXECUTABLE}
        "${CMAKE_SOURCE_DIR}/newview/tests/vksaudio_mock_stream_server.py"
        )
endif ()
