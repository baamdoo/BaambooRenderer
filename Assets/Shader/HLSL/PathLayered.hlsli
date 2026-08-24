#ifndef _HLSL_PATHLAYERED_HEADER
#define _HLSL_PATHLAYERED_HEADER

#include "PathSurface.hlsli"
#include "Sampling.hlsli"

namespace BxDF
{

namespace Layered
{

struct DielectricFrame
{
    uint   bFlipped;
    float  etaTOverI;
    float3 wo;
};

DielectricFrame MakeDielectricFrame(float3 wo, float ior1, float ior2)
{
    DielectricFrame frame;
    frame.bFlipped = wo.z < 0.0 ? 1u : 0u;

    float iorI = frame.bFlipped != 0u ? ior2 : ior1;
    float iorT = frame.bFlipped != 0u ? ior1 : ior2;
    frame.etaTOverI = max(iorT, 1.0e-4) / max(iorI, 1.0e-4);
    frame.wo  = frame.bFlipped != 0u ? -wo : wo;

    return frame;
}

DielectricFrame MakeThinDielectricFrame(float3 wo, float iorI, float iorT)
{
    DielectricFrame frame;
    frame.bFlipped = wo.z < 0.0 ? 1u : 0u;
    frame.etaTOverI = max(iorT, 1.0e-4) / max(iorI, 1.0e-4);
    frame.wo = frame.bFlipped != 0u ? -wo : wo;
    return frame;
}

// One stochastic scattering event at the current stack boundary
struct LayerEvent
{
    float3 wi;     // next propagation direction in the stack frame
    float  pdf;    // Internal mixed-measure sampling quantity: PMF for a delta event
    float3 weight;
    float  etaTOverI;    // transmission: iorT / iorI; reflection: 1

    uint isDelta;
    uint isTransmission;
    uint lobe;
    uint flags;
    uint valid;
};

LayerEvent InitializeLayerEvent()
{
    LayerEvent event;
    event.wi     = float3(0.0, 0.0, 0.0);
    event.weight = float3(0.0, 0.0, 0.0);
    event.pdf    = 0.0;
    event.etaTOverI    = 1.0;

    event.isDelta        = 0u;
    event.isTransmission = 0u;
    event.lobe           = BxDF::LOBE_DIFFUSE;
    event.flags          = 0u;
    event.valid          = 0u;
    return event;
}


} // namespace Layered

} // namespace BxDF

#endif // _HLSL_PATHLAYERED_HEADER
