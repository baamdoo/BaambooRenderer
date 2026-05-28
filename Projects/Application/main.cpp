#include "Applications/BistroApp.h"
#include "Applications/ExampleApp.h"
#include "Applications/LightingApp.h"
#include "Applications/RayTracingApp.h"
#include "Applications/TerrainApp.h"

#include <iostream>

int main()
{
	// TODO: Utilize args
	//eRendererAPI rendererAPI = eRendererAPI::D3D12;
	eRendererAPI rendererAPI = eRendererAPI::Vulkan;

	//ExampleApp app = {};
	//BistroApp app = {};
	//LightingApp app = {};
	//RayTracingApp app = {};
	TerrainApp app = {};
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
