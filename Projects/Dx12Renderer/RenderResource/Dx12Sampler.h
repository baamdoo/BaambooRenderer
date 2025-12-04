#pragma once
#include "Dx12Resource.h"

namespace dx12
{

class Dx12Sampler : public render::Sampler, public Dx12Resource
{
public:
    static Arc< Dx12Sampler > Create(Dx12RenderDevice& rd, const char* name, CreationInfo&& info);

    static Arc< Dx12Sampler > CreateLinearRepeat(Dx12RenderDevice& rd, const char* name = "LinearRepeat");
    static Arc< Dx12Sampler > CreateLinearClamp(Dx12RenderDevice& rd, const char* name = "LinearClamp");
    static Arc< Dx12Sampler > CreatePointRepeat(Dx12RenderDevice& rd, const char* name = "PointRepeat");
    static Arc< Dx12Sampler > CreatePointClamp(Dx12RenderDevice& rd, const char* name = "PointClamp");
    static Arc< Dx12Sampler > CreateLinearClampCmp(Dx12RenderDevice& rd, const char* name = "Shadow");

    Dx12Sampler(Dx12RenderDevice& rd, const char* name, CreationInfo&& info);
    virtual ~Dx12Sampler();

private:
    D3D12_CPU_DESCRIPTOR_HANDLE m_SamplerView = {};
};

} // namespace dx12