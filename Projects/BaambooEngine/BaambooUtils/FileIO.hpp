#pragma once
#include <filesystem>
#include <fstream>
#if defined(_WIN64)
#include <io.h>
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;
namespace FileIO
{

struct Data
{
	void*	data;
	u64		size;

	void Deallocate() { _aligned_free(data); data = nullptr; size = 0; }
};

static bool FileExist(std::string_view filepath)
{
	return _access(filepath.data(), 0) != -1;
}

static Data ReadBinary(std::string_view filepath)
{
	std::ifstream file(filepath.data(), std::ifstream::binary);
	assert(file);

	FileIO::Data outData = {};
	file.seekg(0, file.end);
	outData.size = file.tellg();
	file.seekg(0, file.beg);

	outData.data = (u8*)malloc(outData.size);
	file.read((char*)outData.data, (i64)outData.size);

	return outData;
}

static void WriteBinary(const std::string& filepath, const std::string& filename, Data& data)
{
	fs::path p(filepath + filename.c_str());
	fs::create_directories(p.parent_path());

	std::ofstream file((filepath + filename).data(), std::ios::out | std::ios::binary);
	if (file.is_open())
	{
		file.write((const char*)data.data, (i64)data.size);
		file.close();
	}
}

static std::string GetFileNameFromPath_Extension(std::string_view filename)
{
	const fs::path filepath(filename);
	return filepath.filename().string();
}
static std::string GetFileNameFromPath_NoExtension(std::string_view filename)
{
	const fs::path filepath(filename);
	return filepath.filename().replace_extension().string();
}

} // namespace FileIO

