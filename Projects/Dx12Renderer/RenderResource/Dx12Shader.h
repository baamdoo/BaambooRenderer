#pragma once
#include "Dx12Resource.h"

namespace dx12
{

class Dx12Shader : public render::Shader, public Dx12Resource
{
public:
	static Arc< Dx12Shader > Create(Dx12RenderDevice& rd, const char* name, CreationInfo&& info);

	Dx12Shader(Dx12RenderDevice& rd, const char* name, CreationInfo&& info);
	virtual ~Dx12Shader();

	[[nodiscard]]
	inline LPVOID GetShaderBufferPointer() const { assert(m_d3dShaderBlob); return m_d3dShaderBlob->GetBufferPointer(); }
	[[nodiscard]]
	inline SIZE_T GetShaderBufferSize() const { assert(m_d3dShaderBlob); return m_d3dShaderBlob->GetBufferSize(); }

protected:
	void CopyBytecode(const std::vector< u8 >& bytecode);

private:
	ID3DBlob* m_d3dShaderBlob = nullptr;
};

}