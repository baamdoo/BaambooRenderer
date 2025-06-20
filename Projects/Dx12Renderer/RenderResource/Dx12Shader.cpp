#include "RendererPch.h"
#include "Dx12Shader.h"
#include "RenderDevice/Dx12RenderDevice.h"

#include <fstream>
#include <iostream>
#include <dxcapi.h>

namespace dx12
{

IDxcUtils* Shader::ms_dxcUtils = nullptr;
IDxcCompiler3* Shader::ms_dxcCompiler = nullptr;
u32 Shader::ms_RefCount = 0;

Shader::Shader(RenderDevice& device, const std::wstring& name, CreationInfo&& info)
	: Super(device, name, eResourceType::Shader)
{
    if (name != L"Dummy")
    {
        LoadBinary(info.filepath);
        Reflect();
    }

    ms_RefCount++;
}

Shader::~Shader()
{
    COM_RELEASE(m_d3d12ShaderReflection);
    COM_RELEASE(m_d3dShaderBlob);

    if (!ms_RefCount)
        return;

    u32 refCount = --ms_RefCount;
    if (!refCount)
    {
        COM_RELEASE(ms_dxcCompiler);
        COM_RELEASE(ms_dxcUtils);
    }
}

void Shader::LoadBinary(std::string_view filepath)
{
    std::ifstream fin(filepath.data(), std::ios::binary);

    fin.seekg(0, std::ios_base::end);
    std::ifstream::pos_type size = (int)fin.tellg();
    fin.seekg(0, std::ios_base::beg);

    ThrowIfFailed(
        D3DCreateBlob(size, &m_d3dShaderBlob)
    );

    fin.read((char*)m_d3dShaderBlob->GetBufferPointer(), size);
    fin.close();
}

void Shader::Reflect()
{
    if (!ms_dxcUtils)
    {
        ThrowIfFailed(
            DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&ms_dxcUtils))
        );
    }
    if (!ms_dxcCompiler)
    {
        ThrowIfFailed(
            DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&ms_dxcCompiler))
        );
    }

    DxcBuffer dxcBuffer = {};
    dxcBuffer.Ptr = m_d3dShaderBlob->GetBufferPointer();
    dxcBuffer.Size = m_d3dShaderBlob->GetBufferSize();
    dxcBuffer.Encoding = 0;
    ms_dxcUtils->CreateReflection(&dxcBuffer, IID_PPV_ARGS(&m_d3d12ShaderReflection));
}

Arc< Shader > Shader::Create(RenderDevice& device, const std::wstring& name, CreationInfo&& info)
{
    return MakeArc< Shader >(device, name, std::move(info));
}

}