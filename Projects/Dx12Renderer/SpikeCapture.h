#pragma once
#include <cstdint>

namespace dx12
{

class SpikeCapture
{
public:
    void Update(double gpuFrameMs);

    void ForceCaptureNextFrame();

    // --- Tunable ---
    float    thresholdMs        = 50.0f;   // spike trigger threshold (ms)
    int      captureFrameCount  = 3;       // frames to capture per spike (3-5 if using GPU profile, since GPU profile is 1-2 frames delayed)
    uint64_t cooldownFrames     = 120;     // min frames between captures, prevents flooding

private:
    uint64_t m_frameIdx           = 0;
    uint64_t m_lastCaptureIdx     = 0;
    bool     m_capturing          = false;
    int      m_capFramesRemaining = 0;
};

} // namespace dx12
