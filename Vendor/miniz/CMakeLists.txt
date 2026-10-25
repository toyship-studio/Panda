# miniz 3.1.2 (MIT): zip archives of the plugin store and of the editor updater. The amalgamated
# miniz.c/miniz.h of the release, unchanged. Compiled as C; the zlib-style names are off so nothing
# clashes with a system zlib.
project(miniz C)

add_library(miniz STATIC miniz.c miniz.h)
target_include_directories(miniz PUBLIC .)
target_compile_definitions(miniz PUBLIC MINIZ_NO_ZLIB_COMPATIBLE_NAMES)
