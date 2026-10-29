include_guard()
include(Linking)
include(Prebuilt)

# The default audio backend: the Vulkanstorm engine on vkstorm-audio (Rust,
# linked through vkstorm_ffi; see doc/rust_port.md, rung six). FMOD Studio
# and OpenAL stay optional compile-time selections.
set(vks_audio_default ON)
if(USE_FMODSTUDIO OR FMODSTUDIO OR USE_OPENAL OR OPENAL)
    set(vks_audio_default OFF)
endif()
option(USE_VKS_AUDIO "Enable the Vulkanstorm (Rust) audio engine" ${vks_audio_default})

if(USE_VKS_AUDIO AND (USE_FMODSTUDIO OR USE_OPENAL))
    message(FATAL_ERROR "Select one audio backend: disable USE_VKS_AUDIO when enabling USE_OPENAL or USE_FMODSTUDIO. Use --vksaudio, --openal or --fmodstudio to reset cached selections.")
endif()

if(USE_VKS_AUDIO)
    if(NOT (WINDOWS OR LINUX) OR NOT ADDRESS_SIZE EQUAL 64)
        message(FATAL_ERROR "The Vulkanstorm audio engine supports Windows x64 and Linux x64. Select USE_VKS_AUDIO=OFF and USE_OPENAL=ON or USE_FMODSTUDIO=ON on this platform.")
    endif()
    # Decoder-only LGPL FFmpeg (HE-AAC and Opus parcel music), loaded by the
    # engine at run time from the viewer's directory; nothing links it.
    use_prebuilt_binary(ffmpeg)
    add_library(ll::vksaudio INTERFACE IMPORTED)
    target_compile_definitions(ll::vksaudio INTERFACE LL_VKS_AUDIO=1)
endif()
