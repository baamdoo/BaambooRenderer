#pragma once
#include "Dx12Resource.h"

struct IDxcUtils;
struct IDxcCompiler3;

namespace dx12
{

class Dx12Shader : public render::Shader, public Dx12Resource
{
public:
	struct DescriptorInfo
	{
		std::string name;

		UINT baseRegister;
		UINT numDescriptors;

		D3D_SHADER_INPUT_TYPE       inputType;
		D3D12_DESCRIPTOR_RANGE_TYPE rangeType;
	};

	struct ShaderReflection
	{
		std::unordered_map< u32, std::vector< DescriptorInfo > > descriptors;
	};

	static Arc< Dx12Shader > Create(Dx12RenderDevice& rd, const std::string& name, CreationInfo&& info);

	Dx12Shader(Dx12RenderDevice& rd, const std::string& name, CreationInfo&& info);
	virtual ~Dx12Shader();

	[[nodiscard]]
	inline LPVOID GetShaderBufferPointer() const { assert(m_d3dShaderBlob); return m_d3dShaderBlob->GetBufferPointer(); }
	[[nodiscard]]
	inline SIZE_T GetShaderBufferSize() const { assert(m_d3dShaderBlob); return m_d3dShaderBlob->GetBufferSize(); }

	inline ID3D12ShaderReflection* GetD3D12ShaderReflection() const { return m_d3d12ShaderReflection; }
	inline const ShaderReflection& Reflection() const { return m_Reflection; }

protected:
	void LoadBinary(std::string_view filepath);
	void Reflect();

private:
	ID3DBlob*               m_d3dShaderBlob         = nullptr;
	ID3D12ShaderReflection* m_d3d12ShaderReflection = nullptr;

	ShaderReflection m_Reflection;

	static IDxcUtils* ms_dxcUtils;
	static IDxcCompiler3* ms_dxcCompiler;
	static u32 ms_RefCount;
};

}