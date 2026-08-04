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
	bool bDumpAOV             = false;
	bool bExitAfterDump       = false;
	bool bPathTracerRequested = false;
	std::string pathTracerScene;
	for (int i = 1; i < argc; ++i)
	{
		const std::string_view arg = argv[i];
		if (arg == "--dump-aov" || arg == "--pathtracer-dump-aov")
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

	if (bPathTracerRequested)
	{
		if (pathTracerScene.empty())
			pathTracerScene = bDumpAOV ? "cornell_box" : std::string(RayTracingApp::s_DefaultPathTracerScene);

		RayTracingApp app = {};
		app.ConfigurePathTracerAutomation(bDumpAOV, bExitAfterDump, pathTracerScene);
		try
		{
			app.Initialize(eRendererAPI::D3D12);
		}
		catch (std::runtime_error& e)
		{
			std::cerr << e.what() << '\n';
			return -1;
		}

		return app.Run();
	}

	eRendererAPI api = eRendererAPI::D3D12;
	//eRendererAPI api = eRendererAPI::Vulkan;

	ExampleApp app = {};
	//BistroApp app = {};
	//LightingApp app = {};
	//RayTracingApp app = {};
	//TerrainApp app = {};
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
