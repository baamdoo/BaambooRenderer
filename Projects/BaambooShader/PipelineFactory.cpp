#include "PipelineFactory.h"
#include "Defines.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cwctype>
#include <stdexcept>

namespace baamboo::shader
{

namespace
{


constexpr auto kScanInterval = std::chrono::milliseconds(250);

bool IsShaderFile(const std::wstring& path)
{
	return path.ends_with(L".slang") || path.ends_with(L".slangh");
}

std::wstring PathKey(const std::filesystem::path& path)
{
	std::wstring key = std::filesystem::absolute(path).lexically_normal().generic_wstring();
	std::transform(key.begin(), key.end(), key.begin(), [](wchar_t c) { return static_cast< wchar_t >(std::towlower(c)); });
	return key;
}

std::filesystem::path SourceFolder()
{
	return SHADER_PATH / "Slang";
}

std::vector< std::filesystem::path > SearchPaths()
{
	return { std::filesystem::absolute(SourceFolder()), std::filesystem::absolute("Projects/BaambooCommon"), std::filesystem::absolute("Projects/ThirdParties/openvdb/nanovdb") };
}

std::vector< std::wstring > PathKeys(const std::vector< std::filesystem::path >& paths)
{
	std::vector< std::wstring > keys;
	keys.reserve(paths.size());
	for (const auto& path : paths)
		keys.push_back(PathKey(path));
	return keys;
}

bool IsBindingCompatible(const render::ShaderBindingLayout& oldLayout, const render::ShaderBindingLayout& newLayout, std::string& outReason)
{
	outReason.clear();

	for (const auto& [varName, _] : oldLayout.pushOffsets)
	{
		if (!newLayout.pushOffsets.contains(varName))
			outReason += "'" + varName + "' is no longer in the push block (removed or renamed).\n";
	}
	for (const auto& [varName, _] : newLayout.pushOffsets)
	{
		if (!oldLayout.pushOffsets.contains(varName))
			outReason += "'" + varName + "' is new in the push block.\n";
	}

	return outReason.empty();
}


} // namespace


ShaderObserver::ShaderObserver(std::filesystem::path folder)
	: m_Folder(std::move(folder))
	, m_NextScan(std::chrono::steady_clock::now() + kScanInterval)
{
	Scan([this](const std::wstring& path, std::filesystem::file_time_type writeTime)
		{
			m_Files.emplace(path, File{ writeTime, writeTime });
		});
}

std::vector< std::wstring > ShaderObserver::Poll()
{
	const auto now = std::chrono::steady_clock::now();
	if (now < m_NextScan)
		return {};
	m_NextScan = now + kScanInterval;

	std::vector< std::wstring > changed;
	Scan([this, &changed](const std::wstring& path, std::filesystem::file_time_type writeTime)
		{
			// a file first seen now has never been reported
			auto [it, bNew] = m_Files.try_emplace(path, File{ std::filesystem::file_time_type::min(), writeTime });
			File& file = it->second;

			// a save is reported once its write time has stayed the same for two scans: the editor may still be writing
			if (!bNew && writeTime != file.reported && writeTime == file.lastSeen)
			{
				file.reported = writeTime;
				changed.push_back(PathKey(path));
			}
			file.lastSeen = writeTime;
		});
	return changed;
}

bool ShaderObserver::HasUnseenChange() const
{
	bool bChanged = false;
	Scan([this, &bChanged](const std::wstring& path, std::filesystem::file_time_type writeTime)
		{
			const auto it = m_Files.find(path);
			bChanged |= it == m_Files.end() || it->second.lastSeen != writeTime;
		});
	return bChanged;
}

void ShaderObserver::Scan(const std::function< void(const std::wstring&, std::filesystem::file_time_type) >& visit) const
{
	std::error_code ec;
	for (std::filesystem::recursive_directory_iterator it(m_Folder, ec), end; !ec && it != end; it.increment(ec))
	{
		const std::wstring& path = it->path().native();
		if (!IsShaderFile(path))
			continue;

		std::error_code entryError;
		const auto writeTime = it->last_write_time(entryError);
		if (!entryError)
			visit(path, writeTime);
	}
}


PipelineFactory::PipelineFactory(Compiler& compiler, render::RenderDevice& rd, eTarget target)
	: m_Compiler(compiler)
	, m_Cache(compiler)
	, m_Observer(std::filesystem::absolute(SourceFolder()))
	, m_RenderDevice(rd)
	, m_Target(target)
{
}

Box< render::ComputePipeline > PipelineFactory::CreateAndBuildComputePipeline(const char* name, const ShaderDesc& desc)
{
	using namespace render;

	auto pPipeline = ComputePipeline::Create(m_RenderDevice, name);
	Register(name, *pPipeline, { { eShaderStage::Compute, desc } }, [pPipeline = pPipeline.get()](const auto& shaders)
		{
			pPipeline->SetComputeShader(shaders[0]);
		});
	pPipeline->Build();
	return pPipeline;
}

Box< render::GraphicsPipeline > PipelineFactory::CreateMeshPipeline(const char* name, const MeshPipelineDesc& desc)
{
	using namespace render;

	auto pPipeline = GraphicsPipeline::Create(m_RenderDevice, name);
	Register(name, *pPipeline, { { eShaderStage::Task, desc.task }, { eShaderStage::Mesh, desc.mesh }, { eShaderStage::Fragment, desc.pixel } },
		[pPipeline = pPipeline.get()](const auto& shaders)
		{
			pPipeline->SetMeshShaders(shaders[1], shaders[2], shaders[0]);
		});
	return pPipeline;
}

Box< render::GraphicsPipeline > PipelineFactory::CreateGraphicsPipeline(const char* name, const GraphicsPipelineDesc& desc)
{
	using namespace render;

	auto pPipeline = GraphicsPipeline::Create(m_RenderDevice, name);
	Register(name, *pPipeline, { { eShaderStage::Vertex, desc.vertex }, { eShaderStage::Fragment, desc.pixel } },
		[pPipeline = pPipeline.get()](const auto& shaders)
		{
			pPipeline->SetShaders(shaders[0], shaders[1]);
		});
	return pPipeline;
}

Box< render::RaytracingPipeline > PipelineFactory::CreateRaytracingPipeline(const char* name, const std::filesystem::path& library)
{
	using namespace render;

	auto pPipeline = RaytracingPipeline::Create(m_RenderDevice, name);
	Register(name, *pPipeline, { { eShaderStage::RayGeneration, ShaderDesc{ library, "" } } }, [pPipeline = pPipeline.get()](const auto& shaders)
		{
			pPipeline->SetShaderLibrary(shaders[0]);
		});
	return pPipeline;
}

void PipelineFactory::ReloadChangedShaders()
{
	const std::vector< std::wstring > changedFiles = m_Observer.Poll();
	if (changedFiles.empty())
		return;

	const std::vector< Record* > records = FindRecordsToReload(changedFiles);
	if (records.empty())
		return;

	printf("[Shader] reloading %zu pipeline(s):", records.size());
	for (const Record* pRecord : records)
		printf(" %s", pRecord->name.c_str());
	printf("\n");
	fflush(stdout);

	for (Record* pRecord : records)
	{
		std::string error;
		if (!CompileAndSetShaders(*pRecord, error))
			fprintf(stderr, "[Shader] pipeline '%s' keeps its last working build:\n%s\n", pRecord->name.c_str(), error.c_str());
		pRecord->error = std::move(error);
	}
}

std::vector< PipelineFactory::ReloadError > PipelineFactory::ReloadErrors() const
{
	std::vector< ReloadError > errors;
	for (const Record& record : m_Records)
	{
		if (!record.error.empty())
			errors.push_back({ record.name, record.error });
	}
	return errors;
}

void PipelineFactory::Register(const char* name, render::Pipeline& pipeline, std::vector< Stage >&& stages, SetShadersFn&& fnSetShaders)
{
	Record record = { &pipeline, name, std::move(stages), std::move(fnSetShaders) };

	std::string error;
	if (!CompileAndSetShaders(record, error))
		throw std::runtime_error(error);

	pipeline.SetOnDestroy([this, pPipeline = &pipeline]
		{
			std::erase_if(m_Records, [pPipeline](const Record& record) { return record.pPipeline == pPipeline; });
		});
	m_Records.push_back(std::move(record));
}

std::vector< PipelineFactory::Record* > PipelineFactory::FindRecordsToReload(const std::vector< std::wstring >& changedFiles)
{
	std::vector< Record* > records;
	for (Record& record : m_Records)
	{
		for (Stage& stage : record.stages)
		{
			// force to reload records if a stage is staled to update new include file dependencies
			if (stage.bStale)
			{
				records.push_back(&record);
				break;
			}

			bool hasDependency = false;
			for (const std::wstring& key : changedFiles)
			{
				hasDependency = std::ranges::contains(stage.dependencies, key);
				if (hasDependency)
				{
					records.push_back(&record);
					break;
				}
			}

			if (hasDependency)
				break;
		}
	}

	return records;
}

void PipelineFactory::UpdateStageState(Stage& stage, bool bApplied, std::vector< std::wstring >&& newDependencies)
{
	if (bApplied)
	{
		stage.dependencies = std::move(newDependencies);
		stage.bStale       = false;
	}
	else
	{
		stage.bStale = true;
	}
}

bool PipelineFactory::CompileAndSetShaders(Record& record, std::string& outError)
{
	std::vector< CompiledStage > compiled;
	compiled.reserve(record.stages.size());

	const bool bApplied = CompileStages(record, compiled, outError) && SetShaders(record, compiled, outError);

	// compiling stops at the first stage that fails, so later stages have no result
	for (size_t i = 0; i < record.stages.size(); ++i)
		UpdateStageState(record.stages[i], bApplied, i < compiled.size() ? PathKeys(compiled[i].result.dependencies) : std::vector< std::wstring >{});

	if (bApplied)
	{
		// create pipeline cache
		for (const CompiledStage& stage : compiled)
		{
			if (stage.bFromCache)
				continue;

			// a file saved while the compiler ran would be stored as its new hash next to code made from its old content
			const bool bSavedSinceCompile = m_Observer.HasUnseenChange() || std::ranges::any_of(stage.result.dependencies, [&stage](const std::filesystem::path& dependency)
				{
					std::error_code ec;
					return std::filesystem::last_write_time(dependency, ec) > stage.started || ec;
				});
			if (!bSavedSinceCompile)
				m_Cache.Store(stage.request, stage.result);
		}
	}
	return bApplied;
}

bool PipelineFactory::CompileStages(const Record& record, std::vector< CompiledStage >& outCompiled, std::string& outError)
{
	for (const Stage& stage : record.stages)
	{
		const std::string name = stage.desc.source.stem().string();

		const CompileResult& result = outCompiled.emplace_back(CompileStage(stage)).result;
		if (!result.ok)
		{
			outError = "Shader '" + name + "' failed to compile:\n" + result.diagnostics;
			return false;
		}
		if (!result.diagnostics.empty())
			fprintf(stderr, "[Shader] %s:\n%s\n", name.c_str(), result.diagnostics.c_str());

		// a stage that has no shader yet is being created: there is nothing to compare with
		if (!stage.pShader)
			continue;

		std::string reason;
		if (!IsBindingCompatible(stage.pShader->GetCreationInfo().bindings.value_or(render::ShaderBindingLayout{}), result.bindings.value_or(render::ShaderBindingLayout{}), reason))
		{
			outError = "Shader '" + name + "' no longer matches the C++ code that drives it:\n" + reason;
			return false;
		}
	}
	return true;
}

bool PipelineFactory::SetShaders(Record& record, const std::vector< CompiledStage >& compiled, std::string& outError)
{
	// a save that changes no code leaves the pipeline as it is
	bool isSameCode = true;
	for (size_t i = 0; i < record.stages.size(); ++i)
	{
		const Arc< render::Shader >& pShader = record.stages[i].pShader;
		isSameCode &= pShader && pShader->GetCreationInfo().bytecode == compiled[i].result.code;
	}
	if (isSameCode)
		return true;

	std::vector< Arc< render::Shader > > oldShaders;
	std::vector< Arc< render::Shader > > newShaders;
	for (size_t i = 0; i < record.stages.size(); ++i)
	{
		const Stage&         stage  = record.stages[i];
		const CompileResult& result = compiled[i].result;

		oldShaders.push_back(stage.pShader);
		newShaders.push_back(render::Shader::Create(m_RenderDevice, stage.desc.source.stem().string().c_str(), { .stage = stage.stage, .bytecode = result.code, .bindings = result.bindings }));
	}

	record.fnSetShaders(newShaders);

	// switch old shaders to new shaders in the pipeline if it is already built
	if (record.pPipeline->IsBuilt() && !record.pPipeline->Build())
	{
		record.fnSetShaders(oldShaders);
		outError = "The pipeline could not be built from the new shaders.";
		return false;
	}

	for (size_t i = 0; i < record.stages.size(); ++i)
		record.stages[i].pShader = std::move(newShaders[i]);
	return true;
}

PipelineFactory::CompiledStage PipelineFactory::CompileStage(const Stage& stage)
{
	CompiledStage compiled;

	CompileRequest& request = compiled.request;
	request.sourcePath  = std::filesystem::absolute(SourceFolder() / stage.desc.source);
	request.entryPoint  = stage.desc.entryPoint;
	request.stage       = stage.stage;
	request.target      = m_Target;
	request.searchPaths = SearchPaths();
	request.macros      = { { "PROFILING_LEVEL", std::to_string(PROFILING_LEVEL) }, { "PT_VALIDATION", std::to_string(PT_VALIDATION) } }; // stats code must match the C++ that stages its buffers

	compiled.started    = std::chrono::file_clock::now();
	compiled.bFromCache = m_Cache.Load(request, compiled.result);
	if (!compiled.bFromCache)
	{
		const auto start = std::chrono::steady_clock::now();
		compiled.result = m_Compiler.Compile(request);
		printf("[Shader] %s %s%s%s in %lld ms\n", compiled.result.ok ? "compiled" : "failed to compile", stage.desc.source.stem().string().c_str(), stage.desc.entryPoint.empty() ? "" : ":", stage.desc.entryPoint.c_str(),
			static_cast< long long >(std::chrono::duration_cast< std::chrono::milliseconds >(std::chrono::steady_clock::now() - start).count()));
		fflush(stdout);
	}
	return compiled;
}


} // namespace baamboo::shader
