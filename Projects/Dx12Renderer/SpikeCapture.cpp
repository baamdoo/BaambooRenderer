#include "RendererPch.h"
#include "SpikeCapture.h"

#if defined(BAAMBOO_PIX_CAPTURE)
    #ifndef USE_PIX
    #define USE_PIX
    #endif
    #include <Windows.h>
    #include <pix3.h>
    #include <cstdio>
#endif

namespace dx12
{

void SpikeCapture::Update(double gpuFrameMs)
{
#if defined(BAAMBOO_PIX_CAPTURE)
    if (m_capturing)
    {
        if (--m_capFramesRemaining <= 0)
        {
            PIXEndCapture(/* discard = */ FALSE);
            m_capturing      = false;
            m_lastCaptureIdx = m_frameIdx;
        }
    }
    else if (gpuFrameMs >= double(thresholdMs) &&
             (m_frameIdx - m_lastCaptureIdx) >= cooldownFrames)
    {
        wchar_t path[512];
        std::swprintf(path, 512,
            L"./Output/Saved/baamboo_spike_f%llu_%.1fms.wpix",
            static_cast<unsigned long long>(m_frameIdx), gpuFrameMs);

        PIXCaptureParameters p = {};
        p.GpuCaptureParameters.FileName = path;
        if (SUCCEEDED(PIXBeginCapture(PIX_CAPTURE_GPU, &p)))
        {
            m_capturing          = true;
            m_capFramesRemaining = captureFrameCount;
        }
    }
#else
    (void)gpuFrameMs;
#endif

    ++m_frameIdx;
}

void SpikeCapture::ForceCaptureNextFrame()
{
#if defined(BAAMBOO_PIX_CAPTURE)
    if (m_capturing) return;

    wchar_t path[512];
    std::swprintf(path, 512,
        L"./Output/Saved/baamboo_manual_f%llu.wpix",
        static_cast<unsigned long long>(m_frameIdx));

    PIXCaptureParameters p = {};
    p.GpuCaptureParameters.FileName = path;
    if (SUCCEEDED(PIXBeginCapture(PIX_CAPTURE_GPU, &p)))
    {
        m_capturing          = true;
        m_capFramesRemaining = captureFrameCount;
    }
#endif
}

} // namespace dx12
