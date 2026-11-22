#include "ShaderCompiler.h"
#include "BindingResolver.h"
#include "Utils/ModulePath.hpp"

#include <slang.h>
#include <slang-com-ptr.h>

#include <cstdio>
#include <stdexcept>
#include <string_view>

namespace baamboo::shader
{

struct Compiler::Impl
{
	Slang::ComPtr< slang::IGlobalSession > pGlobalSession;
	std::string                            loadedDxcVersion;
};

namespace
{

struct TargetOptions
{
	SlangCompileTarget                        format;
	const char*                               profile;
	std::vector< slang::CompilerOptionEntry > entries;
	bool                                      scalarBufferLayout = false; // a TargetDesc field: as a session option entry it leaves the layout std140/std430
};

slang::CompilerOptionEntry IntOption(slang::CompilerOptionName name, i32 value)
{
	slang::CompilerOptionEntry entry = {};
	entry.name            = name;
	entry.value.kind      = slang::CompilerOptionValueKind::Int;
	entry.value.intValue0 = value;
	return entry;
}

TargetOptions OptionsFor(eTarget target)
{
	TargetOptions options = {};
	options.format  = target == eTarget::Dxil ? SLANG_DXIL : SLANG_SPIRV;
	options.profile = target == eTarget::Dxil ? "sm_6_6" : "spirv_1_6";
	options.entries = {
		IntOption(slang::CompilerOptionName::MatrixLayoutColumn, 1), // matches glm's memory layout; the API default is row-major
		IntOption(slang::CompilerOptionName::Optimization, SLANG_OPTIMIZATION_LEVEL_MAXIMAL),
		IntOption(slang::CompilerOptionName::DebugInformation, SLANG_DEBUG_INFO_LEVEL_STANDARD), // shader code readable in PIX and RenderDoc
		IntOption(slang::CompilerOptionName::DiagnosticColor, SLANG_DIAGNOSTIC_COLOR_NEVER),
	};
	options.scalarBufferLayout = target == eTarget::Spirv;

	if (target == eTarget::Spirv)
		options.entries.push_back(IntOption(slang::CompilerOptionName::BindlessSpaceIndex, 0));

	return options;
}

SlangStage ToSlangStage(render::eShaderStage stage)
{
	switch (stage)
	{
	case render::eShaderStage::Vertex:        return SLANG_STAGE_VERTEX;
	case render::eShaderStage::Hull:          return SLANG_STAGE_HULL;
	case render::eShaderStage::Domain:        return SLANG_STAGE_DOMAIN;
	case render::eShaderStage::Geometry:      return SLANG_STAGE_GEOMETRY;
	case render::eShaderStage::Fragment:      return SLANG_STAGE_FRAGMENT;
	case render::eShaderStage::Compute:       return SLANG_STAGE_COMPUTE;
	case render::eShaderStage::Task:          return SLANG_STAGE_AMPLIFICATION;
	case render::eShaderStage::Mesh:          return SLANG_STAGE_MESH;
	case render::eShaderStage::RayGeneration: return SLANG_STAGE_RAY_GENERATION;
	case render::eShaderStage::AnyHit:        return SLANG_STAGE_ANY_HIT;
	case render::eShaderStage::ClosestHit:    return SLANG_STAGE_CLOSEST_HIT;
	case render::eShaderStage::Miss:          return SLANG_STAGE_MISS;
	case render::eShaderStage::Interaction:   return SLANG_STAGE_INTERSECTION;
	case render::eShaderStage::Callable:      return SLANG_STAGE_CALLABLE;
	default:                                  return SLANG_STAGE_NONE;
	}
}

std::filesystem::path NormalizePath(const char* utf8Path)
{
	const std::filesystem::path path(reinterpret_cast<const char8_t*>(utf8Path));
	return std::filesystem::absolute(path).lexically_normal().generic_wstring();
}

// Slang takes UTF-8 paths; path::c_str() is wchar_t on Windows.
std::string ToUtf8(const std::filesystem::path& path)
{
	const std::u8string utf8 = path.u8string();
	return std::string(utf8.begin(), utf8.end());
}

bool HasErrorDiagnostic(std::string_view diagnostics)
{
	size_t lineStart = 0;
	while (lineStart < diagnostics.size())
	{
		if (diagnostics.substr(lineStart).starts_with("error["))
			return true;

		const size_t newline = diagnostics.find('\n', lineStart);
		if (newline == std::string_view::npos)
			break;
		lineStart = newline + 1;
	}
	return false;
}

} // namespace

Compiler::Compiler()
	: m_pImpl(MakeBox< Impl >())
{
	// Global session; singleton, same lifecycle as the program
	if (SLANG_FAILED(slang::createGlobalSession(m_pImpl->pGlobalSession.writeRef())))
		throw std::runtime_error("Failed to create the Slang global session!");

	const std::u8string toolDir = ModulePath::ThisModuleDirectory().u8string();
	m_pImpl->pGlobalSession->setDownstreamCompilerPath(SLANG_PASS_THROUGH_DXC, reinterpret_cast< const char* >(toolDir.c_str()));       // DXIL
	m_pImpl->pGlobalSession->setDownstreamCompilerPath(SLANG_PASS_THROUGH_SPIRV_OPT, reinterpret_cast< const char* >(toolDir.c_str())); // SPIRV

	Slang::ComPtr< ISlangBlob > pDxcPath;
	if (SLANG_SUCCEEDED(m_pImpl->pGlobalSession->getDownstreamCompilerPath(SLANG_PASS_THROUGH_DXC, pDxcPath.writeRef())))
	{
		std::string dxcPath(static_cast< const char* >(pDxcPath->getBufferPointer()), pDxcPath->getBufferSize());
		while (!dxcPath.empty() && dxcPath.back() == '\0')
			dxcPath.pop_back();
		m_pImpl->loadedDxcVersion = ModulePath::FileVersion(std::filesystem::path(reinterpret_cast< const char8_t* >(dxcPath.c_str())));
	}
}

Compiler::~Compiler() = default;

CompileResult Compiler::Compile(const CompileRequest& request)
{
	CompileResult result = {};

	TargetOptions options = OptionsFor(request.target);

	const bool bLibrary = request.entryPoint.empty();
	if (bLibrary)
		options.entries.push_back(IntOption(slang::CompilerOptionName::GenerateWholeProgram, 1));

	slang::TargetDesc targetDesc = {};
	targetDesc.format  = options.format;
	targetDesc.profile = m_pImpl->pGlobalSession->findProfile(options.profile);
	targetDesc.forceGLSLScalarBufferLayout = options.scalarBufferLayout;

	slang::TargetDesc targets[] = { targetDesc };

	std::vector< std::string > pathStrings;
	for (const std::filesystem::path& searchPath : request.searchPaths)
		pathStrings.push_back(ToUtf8(searchPath));

	std::vector< const char* > paths;
	for (const std::string& path : pathStrings)
		paths.push_back(path.c_str());

	std::vector< slang::PreprocessorMacroDesc > macros;
	macros.reserve(request.macros.size() + 1);
	for (size_t i = 0; i < request.macros.size(); ++i)
	{
		slang::PreprocessorMacroDesc desc = { request.macros[i].first.data(), request.macros[i].second.data() };
		macros.emplace_back(desc);
	}
	if (request.target == eTarget::Dxil)
		macros.push_back({ "BAAMBOO_DXIL", "1" }); // Slang predefines no macro that tells DXIL from SPIR-V

	slang::SessionDesc desc = {};
	desc.targets     = targets;
	desc.targetCount = 1;
	desc.searchPaths     = paths.data();
	desc.searchPathCount = paths.size();
	desc.preprocessorMacros     = macros.data();
	desc.preprocessorMacroCount = macros.size();
	desc.compilerOptionEntries    = options.entries.data();
	desc.compilerOptionEntryCount = static_cast<uint32_t>(options.entries.size());

	Slang::ComPtr< slang::ISession > pSession;
	const SlangResult sessionResult = m_pImpl->pGlobalSession->createSession(desc, pSession.writeRef());
	if (!SLANG_SUCCEEDED(sessionResult))
	{
		char line[64];
		snprintf(line, sizeof(line), "error[createSession]: failed (0x%08X)\n", static_cast< u32 >(sessionResult));
		result.diagnostics = line;
		return result;
	}

	Slang::ComPtr< slang::IBlob > pDiagnostics;

	const std::string sourcePath = ToUtf8(request.sourcePath);

	slang::IModule* pModule = pSession->loadModuleFromSource("", sourcePath.c_str(), nullptr, pDiagnostics.writeRef());
	if (pDiagnostics)
		result.diagnostics.append(static_cast<const char*>(pDiagnostics->getBufferPointer()), pDiagnostics->getBufferSize());
	if (pModule == nullptr)
		return result;

	std::vector< Slang::ComPtr< slang::IEntryPoint > > entryPoints;
	if (bLibrary)
	{
		for (SlangInt32 i = 0; i < pModule->getDefinedEntryPointCount(); ++i)
		{
			Slang::ComPtr< slang::IEntryPoint > pEntryPoint;
			if (SLANG_SUCCEEDED(pModule->getDefinedEntryPoint(i, pEntryPoint.writeRef())))
				entryPoints.push_back(pEntryPoint);
		}
		if (entryPoints.empty())
		{
			result.diagnostics += "error[entryPoint]: a library needs [shader(...)] entry points\n";
			return result;
		}
	}
	else
	{
		Slang::ComPtr< slang::IEntryPoint > pEntryPoint;
		const SlangResult entryResult = pModule->findAndCheckEntryPoint(request.entryPoint.data(), ToSlangStage(request.stage), pEntryPoint.writeRef(), pDiagnostics.writeRef());
		if (pDiagnostics)
			result.diagnostics.append(static_cast<const char*>(pDiagnostics->getBufferPointer()), pDiagnostics->getBufferSize());
		if (!SLANG_SUCCEEDED(entryResult))
			return result;

		entryPoints.push_back(pEntryPoint);
	}

	std::vector< slang::IComponentType* > components = { pModule };
	for (const Slang::ComPtr< slang::IEntryPoint >& pEntryPoint : entryPoints)
		components.push_back(pEntryPoint.get());

	Slang::ComPtr< slang::IComponentType > pComposite;
	const SlangResult compositeResult = pSession->createCompositeComponentType(components.data(), static_cast< SlangInt >(components.size()), pComposite.writeRef(), pDiagnostics.writeRef());
	if (pDiagnostics)
		result.diagnostics.append(static_cast<const char*>(pDiagnostics->getBufferPointer()), pDiagnostics->getBufferSize());
	if (!SLANG_SUCCEEDED(compositeResult))
		return result;

	Slang::ComPtr< slang::IComponentType > pLink;
	const SlangResult linkResult = pComposite->link(pLink.writeRef(), pDiagnostics.writeRef());
	if (pDiagnostics)
		result.diagnostics.append(static_cast<const char*>(pDiagnostics->getBufferPointer()), pDiagnostics->getBufferSize());
	if (!SLANG_SUCCEEDED(linkResult))
		return result;

	// A whole-program link must use getTargetCode: getEntryPointCode recompiles from scratch and returns one entry
	Slang::ComPtr< slang::IBlob > pCode;
	const SlangResult codeResult = bLibrary ? pLink->getTargetCode(0, pCode.writeRef(), pDiagnostics.writeRef())
	                                        : pLink->getEntryPointCode(0, 0, pCode.writeRef(), pDiagnostics.writeRef());
	if (pDiagnostics)
		result.diagnostics.append(static_cast<const char*>(pDiagnostics->getBufferPointer()), pDiagnostics->getBufferSize());
	if (!SLANG_SUCCEEDED(codeResult))
		return result;

	const u8* p = static_cast<const u8*>(pCode->getBufferPointer());
	result.code.assign(p, p + pCode->getBufferSize());
	for (int i = 0; i < pModule->getDependencyFileCount(); ++i)
		result.dependencies.push_back(NormalizePath(pModule->getDependencyFilePath(i)));

	result.ok = !result.code.empty() && !HasErrorDiagnostic(result.diagnostics);

	if (result.ok)
	{
		render::ShaderBindingLayout bindings;
		std::string                 error;
		if (!ResolveBindings(pLink->getLayout(0), request.target, bindings, error))
		{
			result.diagnostics += "error[bindings]: " + error + "\n";
			result.ok = false;
		}
		else if (bindings.pushBlockSize > render::kSlangPushBlockBytes)
		{
			// pipeline Build() asserts on this: failing here lets a rebuild keep the old pipeline
			result.diagnostics += "error[bindings]: the push block is " + std::to_string(bindings.pushBlockSize) + " bytes, over the "
			                    + std::to_string(render::kSlangPushBlockBytes) + "-byte limit\n";
			result.ok = false;
		}
		else
		{
			result.bindings = std::move(bindings);
		}
	}

	return result;
}

std::string Compiler::OptionsKey(eTarget target) const
{
	const TargetOptions options = OptionsFor(target);

	std::string key = std::to_string(static_cast< int >(options.format)) + ' ' + options.profile + (options.scalarBufferLayout ? " scalar" : "");
	for (const slang::CompilerOptionEntry& entry : options.entries)
		key += ' ' + std::to_string(static_cast< int >(entry.name)) + '=' + std::to_string(entry.value.intValue0);

	return key;
}

const std::string& Compiler::LoadedDxcVersion() const
{
	return m_pImpl->loadedDxcVersion;
}

const char* Compiler::SlangBuildTag() const
{
	return m_pImpl->pGlobalSession->getBuildTagString();
}

} // namespace baamboo::shader
