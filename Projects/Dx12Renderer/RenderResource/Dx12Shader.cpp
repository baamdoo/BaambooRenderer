#include "RendererPch.h"
#include "Dx12Shader.h"
#include "RenderDevice/Dx12RenderDevice.h"

namespace dx12
{

Arc< Dx12Shader > Dx12Shader::Create(Dx12RenderDevice& rd, const char* name, CreationInfo&& info)
{
    return MakeArc< Dx12Shader >(rd, name, std::move(info));
}

Dx12Shader::Dx12Shader(Dx12RenderDevice& rd, const char* name, CreationInfo&& info)
	: render::Shader(name, std::move(info))
    , Dx12Resource(rd, name, eResourceType::Shader)
{
    if (!m_CreationInfo.bytecode.empty())
    {
        CopyBytecode(m_CreationInfo.bytecode);
    }
    BB_ASSERT(m_d3dShaderBlob, "Shader '%s' has no bytecode", name);
}

Dx12Shader::~Dx12Shader()
{
    COM_RELEASE(m_d3dShaderBlob);
}

void Dx12Shader::CopyBytecode(const std::vector< u8 >& bytecode)
{
    ThrowIfFailed(
        D3DCreateBlob(bytecode.size(), &m_d3dShaderBlob)
    );

    memcpy(m_d3dShaderBlob->GetBufferPointer(), bytecode.data(), bytecode.size());
}

}
