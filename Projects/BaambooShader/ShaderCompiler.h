#pragma once
#include "RenderCommon/RenderResources.h"

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace baamboo::shader
{

enum class eTarget
{
	Dxil,  // D3D12
	Spirv, // Vulkan
};

struct CompileRequest
{
	std::filesystem::path                                sourcePath;  // absolute
	std::string                                          entryPoint;
	render::eShaderStage                                 stage  = render::eShaderStage::Compute;
	eTarget                                              target = eTarget::Dxil;
	std::vector< std::filesystem::path >                 searchPaths; // #include lookup
	std::vector< std::pair< std::string, std::string > > macros;      // name, value
};

struct CompileResult
{
	bool                                          ok = false;
	std::vector< u8 >                             code;         // entry point code, or the whole library
	std::string                                   diagnostics;  // every stage's messages
	std::vector< std::filesystem::path >          dependencies; // normalized source files
	std::optional< render::ShaderBindingLayout >  bindings;
};

// Compiles shader source to DXIL or SPIR-V with Slang; Thread-unsafe
class Compiler
{
public:
	Compiler();
	~Compiler();

	Compiler(const Compiler&) = delete;
	Compiler& operator=(const Compiler&) = delete;

	CompileResult Compile(const CompileRequest& request);

	const std::string& LoadedDxcVersion() const; // file version of the dxcompiler.dll Slang loaded
	const char*        SlangBuildTag() const;
	std::string        OptionsKey(eTarget target) const; // the compile options of a target as text, for cache keys

private:
	struct Impl;
	Box< Impl > m_pImpl;
};

} // namespace baamboo::shader
