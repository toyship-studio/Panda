cmake_minimum_required(VERSION 3.19)
project(PandaAudio)

set(CMAKE_CXX_STANDARD 20)

# Audio — an engine module (context/plugins.md, context/audio.md): audio clips as a file-based
# asset kind, the mixer of the application (AudioEngine, an application system), the audio source
# and listener components with their world system, and the "audio" script table.
# panda_package() (PandaSDK/cmake/PandaPackage.cmake): STATIC `PandaAudio` (the engine part) and
# `PandaAudioEditor` (the clip tab, the inspector sections, the AUDIO settings and statistics on
# PandaEditorKit). The device backends, the decoders and the mixing engine are the vendored
# miniaudio (Vendor/miniaudio): the library of the engine tree, or the prebuilt one of the SDK
# package in a game export. Only the engine part's sources see its header.
if (NOT COMMAND panda_package)
    include(${PANDA_PACKAGE_CMAKE})
endif ()

panda_package(STATIC_TARGET PandaAudio EDITOR_STATIC_TARGET PandaAudioEditor)

if (TARGET miniaudio)
    target_link_libraries(PandaAudio PRIVATE miniaudio)
else ()
    target_link_libraries(PandaAudio PRIVATE PandaSDK::miniaudio)
endif ()
