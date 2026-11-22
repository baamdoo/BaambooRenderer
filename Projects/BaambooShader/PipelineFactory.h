#pragma once
#include "ShaderCompiler.h"
#include "ShaderCache.h"

#include <chrono>
#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace baamboo::shader
{

struct ShaderDesc
{
	std::filesystem::path source;     // relative to the shader source folder
	std::string           entryPoint = "main";
};

struct MeshPipelineDesc
{
	ShaderDesc task;
	ShaderDesc mesh;
	ShaderDesc pixel;
};

struct GraphicsPipelineDesc
{
	ShaderDesc vertex;
	ShaderDesc pixel;
};

class ShaderObserver
{
public:
	explicit ShaderObserver(std::filesystem::path folder);

	std::vector< std::wstring > Poll();
	bool HasUnseenChange() const;

private:
	struct File
	{
		std::filesystem::file_time_type reported;
		std::filesystem::file_time_type lastSeen;
	};

	void Scan(const std::function< void(const std::wstring&, std::filesystem::file_time_type) >& visit) const;

	std::filesystem::path                    m_Folder;
	std::unordered_map< std::wstring, File > m_Files; // by the path as the folder scan spells it
	std::chrono::steady_clock::time_point    m_NextScan;
};

class PipelineFactory
{
public:
	PipelineFactory(Compiler& compiler, render::RenderDevice& rd, eTarget target);

	Box< render::ComputePipeline > CreateAndBuildComputePipeline(const char* name, const ShaderDesc& desc);
	Box< render::GraphicsPipeline > CreateMeshPipeline(const char* name, const MeshPipelineDesc& desc);
	Box< render::GraphicsPipeline > CreateGraphicsPipeline(const char* name, const GraphicsPipelineDesc& desc);
	Box< render::RaytracingPipeline > CreateRaytracingPipeline(const char* name, const std::filesystem::path& library);

	// call between frames, with no command context open
	void ReloadChangedShaders();

	struct ReloadError
	{
		std::string pipeline;
		std::string message;
	};
	std::vector< ReloadError > ReloadErrors() const;

private:
	struct Stage
	{
		render::eShaderStage        stage;
		ShaderDesc                  desc;
		Arc< render::Shader >       pShader;
		std::vector< std::wstring > dependencies;   // path keys of the files pShader was compiled from
		bool                        bStale = false; // the files on disk are newer than pShader
	};
	using SetShadersFn = std::function< void(const std::vector< Arc< render::Shader > >&) >; // shaders in stage order

	struct Record
	{
		render::Pipeline*    pPipeline;
		std::string          name;
		std::vector< Stage > stages;
		SetShadersFn         fnSetShaders;
		std::string          error;
	};

	struct CompiledStage
	{
		CompileRequest                  request;
		CompileResult                   result;
		bool                            bFromCache = false;
		std::filesystem::file_time_type started;
	};

	void Register(const char* name, render::Pipeline& pipeline, std::vector< Stage >&& stages, SetShadersFn&& fnSetShaders);

	std::vector< Record* > FindRecordsToReload(const std::vector< std::wstring >& changedFiles);
	void UpdateStageState(Stage& stage, bool bApplied, std::vector< std::wstring >&& newDependencies);

	bool CompileAndSetShaders(Record& record, std::string& outError);
	bool CompileStages(const Record& record, std::vector< CompiledStage >& outCompiled, std::string& outError);
	bool SetShaders(Record& record, const std::vector< CompiledStage >& compiled, std::string& outError);
	CompiledStage CompileStage(const Stage& stage);

	Compiler&             m_Compiler;
	ShaderCache           m_Cache;
	ShaderObserver        m_Observer;
	render::RenderDevice& m_RenderDevice;
	eTarget               m_Target;

	std::vector< Record > m_Records;
};

} // namespace baamboo::shader
