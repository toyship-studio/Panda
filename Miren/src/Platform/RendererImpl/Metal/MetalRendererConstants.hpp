#pragma once

// Buffer slot of the [[stage_in]] vertex stream. dxc hands out bindings to cbuffers
// starting from zero → spirv-cross gives them [[buffer(0..N)]]; the stream is put into the last
// macOS slot (31 buffers) to avoid an overlap.
constexpr int METAL_VERTEX_STREAM_BUFFER_INDEX = 30;
// Instance data stream (stepFunction per-instance) — the neighboring upper slot.
constexpr int METAL_INSTANCE_STREAM_BUFFER_INDEX = 29;
