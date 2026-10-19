#pragma once

#define MIREN_LOG_ENABLED 0
#define MIREN_CMDBUF_LOG_ENABLED 0
constexpr int MAX_VERTEX_BUFFERS = 16384;
constexpr int MAX_INDEX_BUFFERS = 16384;
constexpr int MAX_DYNAMIC_VERTEX_BUFFERS = 16384;
constexpr int MAX_DYNAMIC_INDEX_BUFFERS = 16384;
// 30 was exhausted by the editor: viewport+IDS+shadows+previews+thumbnails+bloom(8)+post ping-pong
// already lived at the ceiling, adding auto exposure (5 targets) made alloc() fail on resize.
constexpr int MAX_FRAME_BUFFERS = 64;
constexpr int MAX_SHADERS = 1000;
constexpr int MAX_TEXTURES = 1000;
constexpr int MAX_BUFFER_LAYOUTS = 1000;
constexpr int TRANSIENT_VERTEX_BUFFER_SIZE = 10000000;
constexpr int TRANSIENT_INDEX_BUFFER_SIZE = 10000000;
constexpr int MAX_DRAW_CALLS = 4096;
constexpr int MAX_UNIFORMS = 100;
constexpr int MAX_FRAMEBUFFER_ATTACHMENTS = 5;
constexpr int MAX_VIEWS = 255;
