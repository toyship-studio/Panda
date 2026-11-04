# miniaudio 0.11.26 (public domain / MIT-0): the audio device backends, the decoders (WAV, FLAC,
# MP3 built in; Vorbis through stb_vorbis from the miniaudio extras) and the engine of the Audio
# module. One implementation translation unit — miniaudio.cpp, or miniaudio.mm on the Apple
# platforms, where the Core Audio backend is Objective-C — plus stb_vorbis.c compiled as C. The
# header lives under include/miniaudio so that the include spelling is the same in the engine tree
# and in the SDK package: #include <miniaudio/miniaudio.h>.
#
# The configuration macros are PUBLIC: the module's translation units include the header with
# the same options the implementation was built with (PandaRuntimeConfig.cmake.in repeats them on
# the imported target of the SDK package).
project(miniaudio C CXX)

if (APPLE)
    set(MINIAUDIO_IMPLEMENTATION miniaudio.mm)
else ()
    set(MINIAUDIO_IMPLEMENTATION miniaudio.cpp)
endif ()

add_library(miniaudio STATIC ${MINIAUDIO_IMPLEMENTATION} stb_vorbis.c include/miniaudio/miniaudio.h)
target_include_directories(miniaudio PUBLIC include)
# No encoders: the engine only plays. The implementation of stb_vorbis is a separate C object; the
# miniaudio unit sees its header part only (STB_VORBIS_HEADER_ONLY, see miniaudio.cpp). miniaudio
# decodes Vorbis through the push-data API of stb_vorbis, so that API stays on.
target_compile_definitions(miniaudio PUBLIC MA_NO_ENCODING)

if (APPLE)
    # Link the frameworks instead of loading them at run time: the released editor is signed
    # with the hardened runtime and notarized (the miniaudio documentation names this option
    # for that case).
    target_compile_definitions(miniaudio PUBLIC MA_NO_RUNTIME_LINKING)
    target_link_libraries(miniaudio PUBLIC
        "-framework CoreFoundation"
        "-framework CoreAudio"
        "-framework AudioToolbox"
    )
    if (PLATFORM_IOS)
        target_link_libraries(miniaudio PUBLIC "-framework AVFoundation") # the audio session
    endif ()
elseif (UNIX)
    # ALSA / PulseAudio / JACK are loaded at run time (dlopen); nothing of them is linked.
    target_link_libraries(miniaudio PUBLIC pthread m dl)
endif ()
