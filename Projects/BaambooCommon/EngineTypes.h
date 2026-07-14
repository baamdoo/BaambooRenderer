#pragma once
#include "Primitives.h"

// CPU-side authoring types shared between engine and common
struct VoxelDiceSettings
{
    u32   maxLevel          = 3u;   // 0 = off, 1..5
    float targetPx          = 2.5f;
    float radiusM           = 40.0f;
    float fadeWidthMeter    = 8.0f;
    float displacementScale = 1.0f;
    u32   debugFlags        = 0u;   // bit0 = dice-level tint

    // Micro displacement band
    float microAmplitudeMeter      = 0.03f;
    float microBaseWaveLengthMeter = 0.35f;
    float microLacunarity          = 2.5f;
    float microGain                = 0.45f;
    float microCreaseBoost         = 1.0f;   // erosion crease/ridge amplitude modulation strength
    float microSharpness           = -0.6f;  // -1 = ridged .. 0 = plain .. +1 = billowed
    u32   microOctaves             = 4u;
};
