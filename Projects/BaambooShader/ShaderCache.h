#pragma once
#include "ShaderCompiler.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace baamboo::shader
{

class ShaderCache
{
public:
	explicit ShaderCache(const Compiler& compiler);

	bool Load(const CompileRequest& request, CompileResult& result) const;
	void Store(const CompileRequest& request, const CompileResult& result) const;

private:
	struct Dependency
	{
		std::filesystem::path path;
		u64                   contentHash = 0;
	};

	struct Entry
	{
		std::string                 key;
		std::vector< Dependency >   dependencies;
		std::vector< u8 >           code;
		render::ShaderBindingLayout bindings;
	};

	std::string MakeKey(const CompileRequest& request) const;
	bool IsValid(const Entry& entry, const std::string& currentKey) const;

	static std::optional< u64 > HashFile(const std::filesystem::path& path);
	static std::filesystem::path EntryPath(const CompileRequest& request);
	static bool ReadEntry(const std::filesystem::path& path, Entry& entry);
	static void WriteEntry(const std::filesystem::path& path, const Entry& entry);

	const Compiler& m_Compiler;
};

} // namespace baamboo::shader
