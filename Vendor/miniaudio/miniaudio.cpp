// The implementation unit of miniaudio (the Apple platforms compile it as Objective-C++ through
// miniaudio.mm). The header part of stb_vorbis comes first so that miniaudio builds its Vorbis
// decoding backend; the functions themselves are compiled as C from stb_vorbis.c of this folder.
#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"
#undef STB_VORBIS_HEADER_ONLY

#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio/miniaudio.h>
