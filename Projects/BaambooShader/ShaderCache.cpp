#include "ShaderCache.h"
#include "Defines.h"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace baamboo::shader
{

namespace
{

// bump when the file layout or the C++ side of binding resolution changes
constexpr u32 kFormatVersion = 1;
constexpr u32 kMagic         = 0x31435342; // "BSC1"

u64 Fnv1a(const void* pData, size_t size, u64 hash = 14695981039346656037ull)
{
	const u8* p = static_cast< const u8* >(pData);
	for (size_t i = 0; i < size; ++i)
	{
		hash ^= p[i];
		hash *= 1099511628211ull;
	}
	return hash;
}

std::string ToUtf8(const std::filesystem::path& path)
{
	const std::u8string utf8 = path.u8string();
	return std::string(utf8.begin(), utf8.end());
}

std::filesystem::path FromUtf8(const std::string& utf8)
{
	return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

class Writer
{
public:
	void U32(u32 value) { Bytes(&value, sizeof(value)); }
	void U64(u64 value) { Bytes(&value, sizeof(value)); }
	void String(const std::string& value) { U32(static_cast< u32 >(value.size())); Bytes(value.data(), value.size()); }
	void Bytes(const void* pData, size_t size)
	{
		const u8* p = static_cast< const u8* >(pData);
		m_Data.insert(m_Data.end(), p, p + size);
	}

	const std::vector< u8 >& Data() const { return m_Data; }

private:
	std::vector< u8 > m_Data;
};


class Reader
{
public:
	explicit Reader(const std::vector< u8 >& data) : m_Data(data) {}

	bool U32(u32& value) { return Bytes(&value, sizeof(value)); }
	bool U64(u64& value) { return Bytes(&value, sizeof(value)); }
	bool String(std::string& value)
	{
		u32 size = 0;
		if (!U32(size) || size > m_Data.size() - m_Offset)
			return false;

		value.assign(reinterpret_cast< const char* >(m_Data.data() + m_Offset), size);
		m_Offset += size;
		return true;
	}
	bool Bytes(void* pData, size_t size)
	{
		if (size > m_Data.size() - m_Offset)
			return false;

		std::memcpy(pData, m_Data.data() + m_Offset, size);
		m_Offset += size;
		return true;
	}
	bool ByteVector(std::vector< u8 >& value)
	{
		u32 size = 0;
		if (!U32(size) || size > m_Data.size() - m_Offset)
			return false;

		value.assign(m_Data.begin() + m_Offset, m_Data.begin() + m_Offset + size);
		m_Offset += size;
		return true;
	}

private:
	const std::vector< u8 >& m_Data;
	size_t                   m_Offset = 0;
};

bool ReadFile(const std::filesystem::path& path, std::vector< u8 >& data)
{
	std::ifstream file(path, std::ios::binary | std::ios::ate);
	if (!file)
		return false;

	const std::streamoff size = file.tellg();
	if (size < 0)
		return false;

	data.resize(static_cast< size_t >(size));
	file.seekg(0, std::ios::beg);
	file.read(reinterpret_cast< char* >(data.data()), size);
	return static_cast< bool >(file);
}

} // namespace


ShaderCache::ShaderCache(const Compiler& compiler)
	: m_Compiler(compiler)
{
}

bool ShaderCache::Load(const CompileRequest& request, CompileResult& result) const
{
	Entry entry;
	if (!ReadEntry(EntryPath(request), entry))
		return false;

	if (!IsValid(entry, MakeKey(request)))
		return false;

	result          = {};
	result.ok       = true;
	result.code     = std::move(entry.code);
	result.bindings = std::move(entry.bindings);
	for (const Dependency& dependency : entry.dependencies)
		result.dependencies.push_back(dependency.path);

	return true;
}

void ShaderCache::Store(const CompileRequest& request, const CompileResult& result) const
{
	if (!result.ok || !result.bindings)
		return;

	Entry entry;
	entry.key      = MakeKey(request);
	entry.code     = result.code;
	entry.bindings = *result.bindings;
	for (const std::filesystem::path& path : result.dependencies)
	{
		const std::optional< u64 > hash = HashFile(path);
		if (!hash)
			return;

		entry.dependencies.push_back({ path, *hash });
	}

	WriteEntry(EntryPath(request), entry);
}

std::string ShaderCache::MakeKey(const CompileRequest& request) const
{
	// each field carries its length, so two different sets of inputs can never join into the same text
	std::string key;
	const auto Add = [&key](const std::string& field)
		{
			key += std::to_string(field.size());
			key += ':';
			key += field;
		};

	Add(ToUtf8(request.sourcePath));
	Add(request.entryPoint);
	Add(std::to_string(static_cast< u32 >(request.stage)));
	Add(std::to_string(static_cast< u32 >(request.target)));

	Add(std::to_string(request.macros.size()));
	for (const auto& [name, value] : request.macros)
	{
		Add(name);
		Add(value);
	}

	Add(std::to_string(request.searchPaths.size()));
	for (const std::filesystem::path& searchPath : request.searchPaths)
		Add(ToUtf8(searchPath));

	Add(m_Compiler.OptionsKey(request.target));
	Add(m_Compiler.SlangBuildTag());
	if (request.target == eTarget::Dxil)
		Add(m_Compiler.LoadedDxcVersion());

	return key;
}

bool ShaderCache::IsValid(const Entry& entry, const std::string& currentKey) const
{
	if (entry.key != currentKey)
		return false;

	for (const Dependency& dependency : entry.dependencies)
	{
		const std::optional< u64 > hash = HashFile(dependency.path);
		if (!hash || *hash != dependency.contentHash)
			return false;
	}

	return true;
}

std::optional< u64 > ShaderCache::HashFile(const std::filesystem::path& path)
{
	std::vector< u8 > data;
	if (!ReadFile(path, data))
		return std::nullopt;

	return Fnv1a(data.data(), data.size());
}

// One file per source, entry point, stage and target, so the variants of one source live side by side.
std::filesystem::path ShaderCache::EntryPath(const CompileRequest& request)
{
	const std::string source = ToUtf8(request.sourcePath);

	u64 hash = Fnv1a(source.data(), source.size());
	hash = Fnv1a(request.entryPoint.data(), request.entryPoint.size(), hash);
	const u32 stage  = static_cast< u32 >(request.stage);
	const u32 target = static_cast< u32 >(request.target);
	hash = Fnv1a(&stage, sizeof(stage), hash);
	hash = Fnv1a(&target, sizeof(target), hash);

	char suffix[32];
	snprintf(suffix, sizeof(suffix), ".%016llx.bin", static_cast< unsigned long long >(hash));

	const std::string name = ToUtf8(request.sourcePath.stem()) + "." + (request.entryPoint.empty() ? "lib" : request.entryPoint)
	                       + (request.target == eTarget::Dxil ? ".dxil" : ".spirv") + suffix;
	return OUTPUT_PATH / "Shader" / "Cache" / FromUtf8(name);
}

bool ShaderCache::ReadEntry(const std::filesystem::path& path, Entry& entry)
{
	std::vector< u8 > data;
	if (!ReadFile(path, data))
		return false;

	Reader reader(data);

	u32 magic = 0, version = 0;
	if (!reader.U32(magic) || !reader.U32(version) || magic != kMagic || version != kFormatVersion)
		return false;

	if (!reader.String(entry.key))
		return false;

	u32 numDependencies = 0;
	if (!reader.U32(numDependencies))
		return false;
	for (u32 i = 0; i < numDependencies; ++i)
	{
		std::string path8;
		u64         hash = 0;
		if (!reader.String(path8) || !reader.U64(hash))
			return false;

		entry.dependencies.push_back({ FromUtf8(path8), hash });
	}

	if (!reader.ByteVector(entry.code))
		return false;

	u32 numPushOffsets = 0;
	if (!reader.U32(entry.bindings.pushBlockSize) || !reader.U32(numPushOffsets))
		return false;
	for (u32 i = 0; i < numPushOffsets; ++i)
	{
		std::string name;
		u32         offset = 0;
		if (!reader.String(name) || !reader.U32(offset))
			return false;

		entry.bindings.pushOffsets.emplace(std::move(name), offset);
	}

	return true;
}

void ShaderCache::WriteEntry(const std::filesystem::path& path, const Entry& entry)
{
	Writer writer;
	writer.U32(kMagic);
	writer.U32(kFormatVersion);
	writer.String(entry.key);

	writer.U32(static_cast< u32 >(entry.dependencies.size()));
	for (const Dependency& dependency : entry.dependencies)
	{
		writer.String(ToUtf8(dependency.path));
		writer.U64(dependency.contentHash);
	}

	writer.U32(static_cast< u32 >(entry.code.size()));
	writer.Bytes(entry.code.data(), entry.code.size());

	writer.U32(entry.bindings.pushBlockSize);
	writer.U32(static_cast< u32 >(entry.bindings.pushOffsets.size()));
	for (const auto& [name, offset] : entry.bindings.pushOffsets)
	{
		writer.String(name);
		writer.U32(offset);
	}

	std::error_code error;
	std::filesystem::create_directories(path.parent_path(), error);

	// write beside the target and rename: a crash mid-write must not leave a short file under the real name
	std::filesystem::path temporary = path;
	temporary += ".tmp";
	{
		std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
		if (!file)
			return;

		file.write(reinterpret_cast< const char* >(writer.Data().data()), static_cast< std::streamsize >(writer.Data().size()));
		if (!file)
			return;
	}
	std::filesystem::rename(temporary, path, error);
}

} // namespace baamboo::shader
