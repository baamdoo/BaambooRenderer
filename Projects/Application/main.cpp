#include "Applications/BistroApp.h"
#include "Applications/ExampleApp.h"
#include "Applications/LightingApp.h"
#include "Applications/RayTracingApp.h"
#include "Applications/TerrainApp.h"

#include <charconv>
#include <algorithm>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace
{
bool ParsePathTracerList(std::string_view value, bool bSeeds, std::vector<u32>& values)
{
	values.clear();
	while (!value.empty())
	{
		const size_t separator = value.find(',');
		const std::string_view item = value.substr(0, separator);
		u32 parsed = 0;
		const auto result = std::from_chars(item.data(), item.data() + item.size(), parsed);
		if (item.empty() || result.ec != std::errc() || result.ptr != item.data() + item.size() ||
			(!bSeeds && (parsed == 0 || (!values.empty() && parsed <= values.back()))) ||
			(bSeeds && std::find(values.begin(), values.end(), parsed) != values.end()))
			return false;
		values.push_back(parsed);
		if (separator == std::string_view::npos)
			return true;
		value.remove_prefix(separator + 1);
	}
	return false;
}
}

int main(int argc, char** argv)
{
	bool bDumpAOV             = false;
	bool bExitAfterDump       = false;
	bool bPathTracerRequested = false;
	std::string pathTracerScene;
	u32 pathTracerSeed = 0;
	u32 pathTracerSamples = 0;
	u32 pathTracerSamplesPerFrame = 0;
	u32 pathTracerMaxDepth = 0;
	std::string pathTracerOutputDir;
	std::vector<u32> pathTracerSeeds;
	std::vector<u32> pathTracerSampleLevels;
	bool bScalarSeedSpecified = false;
	bool bScalarSamplesSpecified = false;
	for (int i = 1; i < argc; ++i)
	{
		const std::string_view arg = argv[i];
		const size_t equalsPosition = arg.find('=');
		const std::string_view option = arg.substr(0, equalsPosition);
		if (option == "--pt-seed" || option == "--pt-spp" || option == "--pt-samples-per-frame" ||
			option == "--pt-max-depth" || option == "--pt-output-dir" ||
			option == "--pt-seeds" || option == "--pt-spp-levels")
		{
			std::string_view value;
			if (equalsPosition != std::string_view::npos)
				value = arg.substr(equalsPosition + 1);
			else if (i + 1 < argc)
				value = argv[++i];

			if (value.empty() || value.starts_with("--"))
			{
				std::cerr << "Missing value for " << option << '\n';
				return -1;
			}
			if (option == "--pt-output-dir")
				pathTracerOutputDir = value;
			else if (option == "--pt-seeds" || option == "--pt-spp-levels")
			{
				const bool bSeeds = option == "--pt-seeds";
				if (!ParsePathTracerList(value, bSeeds, bSeeds ? pathTracerSeeds : pathTracerSampleLevels))
				{
					std::cerr << "Invalid " << option << ": expected "
						<< (bSeeds ? "unique uint32 seeds" : "strictly increasing positive uint32 sample counts")
						<< " separated by commas\n";
					return -1;
				}
			}
			else
			{
				u32 parsed = 0;
				const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
				if (result.ec != std::errc() || result.ptr != value.data() + value.size() ||
					(option != "--pt-seed" && parsed == 0))
				{
					std::cerr << "Invalid value for " << option << ": " << value
						<< " (expected " << (option == "--pt-seed" ? "an unsigned 32-bit integer" : "a positive 32-bit integer") << ")\n";
					return -1;
				}
				if (option == "--pt-seed")
				{
					pathTracerSeed = parsed;
					bScalarSeedSpecified = true;
				}
				else if (option == "--pt-spp")
				{
					pathTracerSamples = parsed;
					bScalarSamplesSpecified = true;
				}
				else if (option == "--pt-max-depth")
					pathTracerMaxDepth = parsed;
				else
					pathTracerSamplesPerFrame = parsed;
			}
			bPathTracerRequested = true;
		}
		else if (arg == "--dump-aov" || arg == "--pathtracer-dump-aov")
		{
			bDumpAOV = true;
			bPathTracerRequested = true;
		}
		else if (arg == "--pathtracer" || arg == "--pt")
		{
			bPathTracerRequested = true;
		}
		else if (arg == "--exit-after-dump" || arg == "--pathtracer-exit-after-dump")
		{
			bDumpAOV = true;
			bExitAfterDump = true;
			bPathTracerRequested = true;
		}
		else if ((arg == "--pathtracer-scene" || arg == "--pt-scene") && i + 1 < argc)
		{
			pathTracerScene = argv[++i];
			bPathTracerRequested = true;
		}
		else if (arg.starts_with("--pathtracer-scene="))
		{
			pathTracerScene = std::string(arg.substr(std::string_view("--pathtracer-scene=").size()));
			bPathTracerRequested = true;
		}
		else if (arg.starts_with("--pt-scene="))
		{
			pathTracerScene = std::string(arg.substr(std::string_view("--pt-scene=").size()));
			bPathTracerRequested = true;
		}
	}

	const bool bBatch = !pathTracerSeeds.empty() || !pathTracerSampleLevels.empty();
	if (bBatch)
	{
		if (pathTracerOutputDir.empty() ||
			(bScalarSeedSpecified && !pathTracerSeeds.empty()) ||
			(bScalarSamplesSpecified && !pathTracerSampleLevels.empty()))
		{
			std::cerr << "Path tracer batches require --pt-output-dir and cannot combine --pt-seed/--pt-seeds or --pt-spp/--pt-spp-levels\n";
			return -1;
		}
		bDumpAOV = true;
	}

	if (bPathTracerRequested)
	{
		if (pathTracerScene.empty())
			pathTracerScene = bDumpAOV ? "cornell_box" : std::string(RayTracingApp::s_DefaultPathTracerScene);

		RayTracingApp app = {};
		app.ConfigurePathTracerAutomation(bDumpAOV, bExitAfterDump, pathTracerScene,
			pathTracerSeed, pathTracerSamples, pathTracerSamplesPerFrame, pathTracerMaxDepth, pathTracerOutputDir);
		if (bBatch)
			app.ConfigurePathTracerBatch(pathTracerSeeds, pathTracerSampleLevels);
		try
		{
			app.Initialize(eRendererAPI::D3D12);
		}
		catch (std::runtime_error& e)
		{
			std::cerr << e.what() << '\n';
			return -1;
		}

		const i32 exitCode = app.Run();
		return app.HasPathTracerAutomationFailed() ? 1 : exitCode;
	}

	eRendererAPI api = eRendererAPI::D3D12;
	//eRendererAPI api = eRendererAPI::Vulkan;

	//ExampleApp app = {};
	//BistroApp app = {};
	//LightingApp app = {};
	//RayTracingApp app = {};
	TerrainApp app = {};
	try
	{
		app.Initialize(api);
	}
	catch (std::runtime_error& e)
	{
		std::cerr << e.what() << '\n';
		return -1;
	}

	return app.Run();
}
