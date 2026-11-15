project(bc7enc)

# BC7 block compression by Richard Geldreich, Jr. (MIT or public domain, see LICENSE):
# bc7enc — the encoder (modes 1 and 6), from github.com/richgel999/bc7enc; the editor cooks
#          textures with it. bc7enc.c is built as C++ (the header declares inline functions).
# bc7decomp — the decoder, from github.com/richgel999/bc7enc_rdo; the engine decodes a cooked
#          texture with it where the device has no BC formats or the pixels are asked for.
add_library(bc7enc STATIC bc7enc.cpp bc7enc.h)
target_include_directories(bc7enc PUBLIC .)

add_library(bc7decomp STATIC bc7decomp.cpp bc7decomp.h)
target_include_directories(bc7decomp PUBLIC .)
