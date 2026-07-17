#pragma once
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>

namespace fs = std::filesystem;
namespace FileIO
{

struct Data
{
	void* data = nullptr;
	u64   size = 0;

	void Deallocate() { std::free(data); data = nullptr; size = 0; }
};

inline Data ReadBinary(std::string_view filepath)
{
	std::ifstream file(std::string(filepath), std::ios::binary | std::ios::ate);
	if (!file)
		return {};

	const std::streamoff fileSize = file.tellg();
	if (fileSize <= 0 ||
		static_cast<u64>(fileSize) > static_cast<u64>(std::numeric_limits<std::size_t>::max()) ||
		static_cast<u64>(fileSize) > static_cast<u64>(std::numeric_limits<std::streamsize>::max()))
	{
		return {};
	}

	FileIO::Data outData = {};
	outData.size = static_cast<u64>(fileSize);
	outData.data = std::malloc(static_cast<std::size_t>(outData.size));
	if (!outData.data)
		return {};

	file.seekg(0, std::ios::beg);
	file.read(static_cast<char*>(outData.data), static_cast<std::streamsize>(outData.size));
	if (!file)
	{
		outData.Deallocate();
		return {};
	}

	return outData;
}

inline void WriteBinary(const std::string& filepath, const std::string& filename, Data& data)
{
	if (!data.data || data.size == 0 ||
		data.size > static_cast<u64>(std::numeric_limits<std::streamsize>::max()))
	{
		return;
	}

	fs::path p(filepath + filename.c_str());
	fs::create_directories(p.parent_path());

	std::ofstream file(p, std::ios::out | std::ios::binary | std::ios::trunc);
	if (file.is_open())
	{
		file.write(static_cast<const char*>(data.data), static_cast<std::streamsize>(data.size));
		file.close();
	}
}

} // namespace FileIO

