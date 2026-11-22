#pragma once
// Shared by C++ and Slang: every GPU struct in this folder is compiled by both, so it uses only their common syntax.
#ifdef __cplusplus
#include "MathTypes.h"

#define BB_GPU_ALIGN(n) alignas(n)

inline const mat4 kIdentity4x4 = mat4(1.0f);
#else
typealias u32  = uint;
typealias i32  = int;
typealias f32  = float;
typealias mat4 = float4x4;

#define BB_GPU_ALIGN(n)

static const u32 kInvalidIndex = 0xffffffffu;
// Not mat4(1.0f): in Slang that fills every element with 1, while glm builds the identity.
static const mat4 kIdentity4x4 = mat4(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1);
#endif
