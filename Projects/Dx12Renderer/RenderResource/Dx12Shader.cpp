#include "RendererPch.h"
#include "Dx12Shader.h"
#include "RenderDevice/Dx12RenderDevice.h"

#include <fstream>
#include <iostream>
#include <dxcapi.h>

namespace dx12
{

IDxcUtils* Dx12Shader::ms_dxcUtils = nullptr;
IDxcCompiler3* Dx12Shader::ms_dxcCompiler = nullptr;
u32 Dx12Shader::ms_RefCount = 0;

#define DX12_SHADER_PATH(filename) GetCompiledShaderPath(filename)
std::string GetCompiledShaderPath(const std::string& filename)
{
	return CSO_PATH.string() + filename + ".cso";
}

static std::string GenerateResourceKey(const D3D12_SHADER_INPUT_BIND_DESC& bindDesc)
{
    return std::string(bindDesc.Name) + "_" +
        std::to_string(bindDesc.Type) + "_" +
        std::to_string(bindDesc.BindPoint) + "_" +
        std::to_string(bindDesc.Space);
}


Arc< Dx12Shader > Dx12Shader::Create(Dx12RenderDevice& rd, const std::string& name, CreationInfo&& info)
{
    return MakeArc< Dx12Shader >(rd, name, std::move(info));
}

Dx12Shader::Dx12Shader(Dx12RenderDevice& rd, const std::string& name, CreationInfo&& info)
	: render::Shader(name, std::move(info))
    , Dx12Resource(rd, name, eResourceType::Shader)
{
    if (name != "Dummy")
    {
        LoadBinary(DX12_SHADER_PATH(m_CreationInfo.filename));
        Reflect();
    }

    ms_RefCount++;
}

Dx12Shader::~Dx12Shader()
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

void Dx12Shader::LoadBinary(std::string_view filepath)
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

void Dx12Shader::Reflect()
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
    dxcBuffer.Ptr      = m_d3dShaderBlob->GetBufferPointer();
    dxcBuffer.Size     = m_d3dShaderBlob->GetBufferSize();
    dxcBuffer.Encoding = 0;
    ms_dxcUtils->CreateReflection(&dxcBuffer, IID_PPV_ARGS(&m_d3d12ShaderReflection));
    assert(m_d3d12ShaderReflection);

    D3D12_SHADER_DESC shaderDesc;
    m_d3d12ShaderReflection->GetDesc(&shaderDesc);

    // Handle CBV separately for root constant
    for (UINT i = 0; i < shaderDesc.ConstantBuffers; ++i)
    {
        ID3D12ShaderReflectionConstantBuffer* pCBuffer = m_d3d12ShaderReflection->GetConstantBufferByIndex(i);
        
        D3D12_SHADER_BUFFER_DESC bufferDesc;
        pCBuffer->GetDesc(&bufferDesc);
        if (bufferDesc.Type != D3D_CT_CBUFFER)
            continue;

        // exclude system buffers (ex. $Globals)
        if (bufferDesc.Name && bufferDesc.Name[0] != '$')
        {
            D3D12_SHADER_INPUT_BIND_DESC bindDesc;
            for (UINT j = 0; j < shaderDesc.BoundResources; ++j)
            {
                m_d3d12ShaderReflection->GetResourceBindingDesc(j, &bindDesc);
                if (bindDesc.Type == D3D_SIT_CBUFFER && strcmp(bindDesc.Name, bufferDesc.Name) == 0)
                {
                    DescriptorInfo& info = m_Reflection.descriptors[bindDesc.Space].emplace_back();
                    info.name           = bindDesc.Name;
                    info.baseRegister   = bindDesc.BindPoint;
                    info.numDescriptors = bindDesc.Space == ROOT_CONSTANT_SPACE ? bufferDesc.Size / 4 : bindDesc.BindCount;
                    info.inputType      = bindDesc.Type;
                    info.rangeType      = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
                    break;
                }
            }
        }
    }

    for (UINT i = 0; i < shaderDesc.BoundResources; ++i)
    {
        D3D12_SHADER_INPUT_BIND_DESC bindDesc;
        m_d3d12ShaderReflection->GetResourceBindingDesc(i, &bindDesc);

        switch (bindDesc.Type)
        {
        //case D3D_SIT_CBUFFER:
        //case D3D_SIT_TBUFFER:
        //{
        //    if (bindDesc.Name && bindDesc.Name[0] != '$')
        //    {
        //        DescriptorInfo& info = m_Reflection.descriptors[bindDesc.Space].emplace_back();
        //        info.name            = bindDesc.Name;
        //        info.baseRegister    = bindDesc.BindPoint;
        //        info.numDescriptors  = bindDesc.BindCount;
        //        info.descType        = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
        //    }
        //    break;
        //}

        case D3D_SIT_TEXTURE:
        case D3D_SIT_STRUCTURED:
        case D3D_SIT_BYTEADDRESS:
        {
            DescriptorInfo& info = m_Reflection.descriptors[bindDesc.Space].emplace_back();
            info.name            = bindDesc.Name;
            info.baseRegister    = bindDesc.BindPoint;
            info.numDescriptors  = bindDesc.BindCount;
            info.inputType       = bindDesc.Type;
            info.rangeType       = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            break;
        }

        case D3D_SIT_UAV_RWTYPED:
        case D3D_SIT_UAV_RWSTRUCTURED:
        case D3D_SIT_UAV_RWBYTEADDRESS:
        case D3D_SIT_UAV_APPEND_STRUCTURED:
        case D3D_SIT_UAV_CONSUME_STRUCTURED:
        case D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER:
        {
            DescriptorInfo& info = m_Reflection.descriptors[bindDesc.Space].emplace_back();
            info.name            = bindDesc.Name;
            info.baseRegister    = bindDesc.BindPoint;
            info.numDescriptors  = bindDesc.BindCount;
            info.inputType       = bindDesc.Type;
            info.rangeType       = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
            break;
        }
        case D3D_SIT_SAMPLER:
        {
            DescriptorInfo& info = m_Reflection.descriptors[bindDesc.Space].emplace_back();
            info.name            = bindDesc.Name;
            info.baseRegister    = bindDesc.BindPoint;
            info.numDescriptors  = bindDesc.BindCount;
            info.inputType       = bindDesc.Type;
            info.rangeType       = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
            break;
        }
        }
    }
}

}