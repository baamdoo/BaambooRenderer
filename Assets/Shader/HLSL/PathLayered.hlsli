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
    float  eta;
    float3 wo;
};

DielectricFrame MakeDielectricFrame(float3 wo, float etaAbove, float etaBelow)
{
    DielectricFrame frame;
    frame.bFlipped = wo.z < 0.0 ? 1u : 0u;

    float etaI = frame.bFlipped != 0u ? etaBelow : etaAbove;
    float etaT = frame.bFlipped != 0u ? etaAbove : etaBelow;
    frame.eta = max(etaT, 1.0e-4) / max(etaI, 1.0e-4);
    frame.wo  = frame.bFlipped != 0u ? -wo : wo;

    return frame;
}

// One stochastic scattering event at the current stack boundary
struct LayerEvent
{
    float3 wi;     // next propagation direction in the stack frame
    float  pdf;    // Internal mixed-measure sampling quantity: PMF for a delta event
    float3 weight;
    float  eta;    // transmission: etaT / etaI; reflection: 1

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
    event.eta    = 1.0;

    event.isDelta        = 0u;
    event.isTransmission = 0u;
    event.lobe           = BxDF::LOBE_DIFFUSE;
    event.flags          = 0u;
    event.valid          = 0u;
    return event;
}

bool IsLayerEventValid(LayerEvent event)
{
    return event.valid != 0u &&
           IsPathFinite3(event.wi) &&
           dot(event.wi, event.wi) > EPSILON_MIN &&
           IsPathFinite3(event.weight) &&
           all(event.weight >= 0.0) &&
           IsPathFinite(event.pdf) &&
           event.pdf > 0.0 &&
           IsPathFinite(event.eta) &&
           event.eta > 0.0;
}

} // namespace Layered

} // namespace BxDF

#endif // _HLSL_PATHLAYERED_HEADER
