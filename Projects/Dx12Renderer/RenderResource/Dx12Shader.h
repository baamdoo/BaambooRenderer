#pragma once
#include "Dx12Resource.h"

struct IDxcUtils;
struct IDxcCompiler3;

namespace dx12
{

class Shader : public Resource
{
using Super = Resource;

public:
	struct CreationInfo
	{
		std::string_view filepath;
	};

	static Arc< Shader > Create(RenderDevice& device, const std::wstring& name, CreationInfo&& info);

	Shader(RenderDevice& device, const std::wstring& name, CreationInfo&& info);
	virtual ~Shader();

	[[nodiscard]]
	inline LPVOID GetShaderBufferPointer() const { assert(m_d3dShaderBlob); return m_d3dShaderBlob->GetBufferPointer(); }
	[[nodiscard]]
	inline SIZE_T GetShaderBufferSize() const { assert(m_d3dShaderBlob); return m_d3dShaderBlob->GetBufferSize(); }

	[[nodiscard]]
	inline ID3D12ShaderReflection* GetShaderReflection() const { return m_d3d12ShaderReflection; }

protected:
	void LoadBinary(std::string_view filepath);
	void Reflect();

private:
	ID3DBlob*               m_d3dShaderBlob = nullptr;
	ID3D12ShaderReflection* m_d3d12ShaderReflection = nullptr;

	static IDxcUtils* ms_dxcUtils;
	static IDxcCompiler3* ms_dxcCompiler;
	static u32 ms_RefCount;
};

}