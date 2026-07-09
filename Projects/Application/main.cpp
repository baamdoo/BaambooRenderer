#include "Applications/BistroApp.h"
#include "Applications/ExampleApp.h"
#include "Applications/LightingApp.h"
#include "Applications/RayTracingApp.h"
#include "Applications/TerrainApp.h"

#include <iostream>
#include <string>
#include <string_view>

int main(int argc, char** argv)
{
	eRendererAPI rendererAPI = eRendererAPI::D3D12;
	//eRendererAPI rendererAPI = eRendererAPI::Vulkan;

	bool bDumpAOV       = false;
	bool bExitAfterDump = false;
	std::string pathTracerScene = "cornell_box";
	for (int i = 1; i < argc; ++i)
	{
		const std::string_view arg = argv[i];
		if (arg == "--dump-aov" || arg == "--pathtracer-dump-aov")
			bDumpAOV = true;
		else if (arg == "--exit-after-dump" || arg == "--pathtracer-exit-after-dump")
			bExitAfterDump = true;
		else if ((arg == "--pathtracer-scene" || arg == "--pt-scene") && i + 1 < argc)
			pathTracerScene = argv[++i];
		else if (arg.starts_with("--pathtracer-scene="))
			pathTracerScene = std::string(arg.substr(std::string_view("--pathtracer-scene=").size()));
		else if (arg.starts_with("--pt-scene="))
			pathTracerScene = std::string(arg.substr(std::string_view("--pt-scene=").size()));
	}

	//ExampleApp app = {};
	//BistroApp app = {};
	//LightingApp app = {};
	RayTracingApp app = {};
	app.ConfigurePathTracerAutomation(bDumpAOV, bExitAfterDump, pathTracerScene);
	//TerrainApp app = {};
	try
	{
		app.Initialize(rendererAPI);
	}
	catch (std::runtime_error& e)
	{
		std::cerr << e.what() << '\n';
		return -1;
	}

	return app.Run();
}
