#pragma once
#include <filesystem>
#include <format>
#include <string>
#include <vector>
#include <Windows.h>

#pragma comment(lib, "version.lib")

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace ModulePath
{

inline std::filesystem::path ThisModuleDirectory()
{
	std::wstring path(MAX_PATH, L'\0');
	for (;;)
	{
		const DWORD length = GetModuleFileNameW(reinterpret_cast< HMODULE >(&__ImageBase), path.data(), static_cast< DWORD >(path.size()));
		if (length == 0)
			return {};
		if (length < path.size())
		{
			path.resize(length);
			return std::filesystem::path(path).parent_path();
		}
		path.resize(path.size() * 2);
	}
}

inline std::string FileVersion(const std::filesystem::path& file)
{
	DWORD handle = 0;
	const DWORD size = GetFileVersionInfoSizeW(file.c_str(), &handle);
	if (size == 0)
		return {};

	std::vector< BYTE > data(size);
	VS_FIXEDFILEINFO* pInfo  = nullptr;
	UINT              length = 0;
	if (!GetFileVersionInfoW(file.c_str(), 0, size, data.data())
		|| !VerQueryValueW(data.data(), L"\\", reinterpret_cast< void** >(&pInfo), &length) || !pInfo)
		return {};

	return std::format("{}.{}.{}.{}", HIWORD(pInfo->dwFileVersionMS), LOWORD(pInfo->dwFileVersionMS),
		HIWORD(pInfo->dwFileVersionLS), LOWORD(pInfo->dwFileVersionLS));
}

} // namespace ModulePath
