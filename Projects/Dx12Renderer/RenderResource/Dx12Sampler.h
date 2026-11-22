#pragma once
#include "Dx12Resource.h"

namespace dx12
{

class Dx12Sampler : public render::Sampler, public Dx12Resource
{
public:
    static Arc< Dx12Sampler > Create(Dx12RenderDevice& rd, const char* name, CreationInfo&& info);

    Dx12Sampler(Dx12RenderDevice& rd, const char* name, CreationInfo&& info);
    virtual ~Dx12Sampler();

private:
    D3D12_CPU_DESCRIPTOR_HANDLE m_SamplerView = {};
};

} // namespace dx12