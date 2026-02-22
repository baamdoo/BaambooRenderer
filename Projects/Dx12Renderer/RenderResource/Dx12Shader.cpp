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

namespace
{

#define DX12_SHADER_PATH(filename) GetCompiledShaderPath(filename)
std::string GetCompiledShaderPath(const std::string& filename)
{
    return CSO_PATH.string() + filename + ".cso";
}

std::string GenerateResourceKey(const D3D12_SHADER_INPUT_BIND_DESC& bindDesc)
{
    return std::string(bindDesc.Name) + "_" +
        std::to_string(bindDesc.Type) + "_" +
        std::to_string(bindDesc.BindPoint) + "_" +
        std::to_string(bindDesc.Space);
}

template< typename TReflection >
void ParseDescriptors(TReflection* pReflection, Dx12Shader::ShaderReflection& reflectionData, UINT boundResources, UINT constantBuffers)
{
    // Handle CBV separately for root constant
    for (UINT i = 0; i < constantBuffers; ++i)
    {
        ID3D12ShaderReflectionConstantBuffer* pCBuffer = pReflection->GetConstantBufferByIndex(i);

        D3D12_SHADER_BUFFER_DESC cbDesc;
        pCBuffer->GetDesc(&cbDesc);
        if (cbDesc.Type != D3D_CT_CBUFFER)
            continue;

        // exclude system buffers (ex. $Globals)
        if (cbDesc.Name && cbDesc.Name[0] != '$')
        {
            D3D12_SHADER_INPUT_BIND_DESC bindDesc;
            bool bFound = false;
            for (UINT j = 0; j < boundResources; ++j)
            {
                pReflection->GetResourceBindingDesc(j, &bindDesc);
                if (bindDesc.Type == D3D_SIT_CBUFFER && strcmp(bindDesc.Name, cbDesc.Name) == 0)
                {
                    bFound = true;
                    break;
                }
            }

            if (bFound)
            {
                // check duplication (multiple functions can share CBVs in the library)
                auto& descriptors = reflectionData.descriptors[bindDesc.Space];
                auto it = std::find_if(descriptors.begin(), descriptors.end(), [&](const Dx12Shader::DescriptorInfo& info) 
                    {
                        return info.baseRegister == bindDesc.BindPoint && info.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
                    });

                if (it == descriptors.end())
                {
                    Dx12Shader::DescriptorInfo& info = descriptors.emplace_back();
                    info.name           = bindDesc.Name;
                    info.baseRegister   = bindDesc.BindPoint;
                    info.numDescriptors = bindDesc.Space == ROOT_CONSTANT_SPACE || MISS_ARGUMENT_SPACE || HITGROUP_ARGUMENT_SPACE ? cbDesc.Size / 4 : bindDesc.BindCount;
                    info.inputType      = bindDesc.Type;
                    info.rangeType      = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
                }
            }
        }
    }

    for (UINT i = 0; i < boundResources; ++i)
    {
        D3D12_SHADER_INPUT_BIND_DESC bindDesc;
        pReflection->GetResourceBindingDesc(i, &bindDesc);

        bool bIsValid = false;
        D3D12_DESCRIPTOR_RANGE_TYPE rangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        switch (bindDesc.Type)
        {
        case D3D_SIT_CBUFFER:
            continue;

        case D3D_SIT_TEXTURE:
        case D3D_SIT_STRUCTURED:
        case D3D_SIT_BYTEADDRESS:
        case D3D_SIT_RTACCELERATIONSTRUCTURE:
            rangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            bIsValid = true;
            break;

        case D3D_SIT_UAV_RWTYPED:
        case D3D_SIT_UAV_RWSTRUCTURED:
        case D3D_SIT_UAV_RWBYTEADDRESS:
        case D3D_SIT_UAV_APPEND_STRUCTURED:
        case D3D_SIT_UAV_CONSUME_STRUCTURED:
        case D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER:
            rangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
            bIsValid = true;
            break;

        case D3D_SIT_SAMPLER:
            rangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
            bIsValid = true;
            break;
        }

        if (bIsValid)
        {
            auto& descriptors = reflectionData.descriptors[bindDesc.Space];
            auto it = std::find_if(descriptors.begin(), descriptors.end(), [&](const Dx12Shader::DescriptorInfo& info) 
                {
                    return info.baseRegister == bindDesc.BindPoint && info.rangeType == rangeType;
                });

            if (it == descriptors.end())
            {
                Dx12Shader::DescriptorInfo& info = descriptors.emplace_back();
                info.name           = bindDesc.Name;
                info.baseRegister   = bindDesc.BindPoint;
                info.numDescriptors = bindDesc.BindCount;
                info.inputType      = bindDesc.Type;
                info.rangeType      = rangeType;
            }
        }
    }
}

}


Arc< Dx12Shader > Dx12Shader::Create(Dx12RenderDevice& rd, const char* name, CreationInfo&& info)
{
    return MakeArc< Dx12Shader >(rd, name, std::move(info));
}

Dx12Shader::Dx12Shader(Dx12RenderDevice& rd, const char* name, CreationInfo&& info)
	: render::Shader(name, std::move(info))
    , Dx12Resource(rd, name, eResourceType::Shader)
{
    LoadBinary(DX12_SHADER_PATH(m_CreationInfo.filename));
    Reflect();

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
    if (render::IsRaytracingShader(m_CreationInfo.stage))
    {
        ThrowIfFailed(ms_dxcUtils->CreateReflection(&dxcBuffer, IID_PPV_ARGS(&m_d3d12LibraryReflection)));

        D3D12_LIBRARY_DESC libDesc;
        m_d3d12LibraryReflection->GetDesc(&libDesc);
        for (UINT i = 0; i < libDesc.FunctionCount; ++i)
        {
            ID3D12FunctionReflection* pFunc = m_d3d12LibraryReflection->GetFunctionByIndex(i);
            D3D12_FUNCTION_DESC funcDesc;
            pFunc->GetDesc(&funcDesc);

            ParseDescriptors(pFunc, m_Reflection, funcDesc.BoundResources, funcDesc.ConstantBuffers);
        }
    }
    else
    {
        ThrowIfFailed(ms_dxcUtils->CreateReflection(&dxcBuffer, IID_PPV_ARGS(&m_d3d12ShaderReflection)));
        assert(m_d3d12ShaderReflection);

        D3D12_SHADER_DESC shaderDesc;
        m_d3d12ShaderReflection->GetDesc(&shaderDesc);

        ParseDescriptors(m_d3d12ShaderReflection, m_Reflection, shaderDesc.BoundResources, shaderDesc.ConstantBuffers);
    }
}

}