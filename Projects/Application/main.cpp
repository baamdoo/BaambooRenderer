#include <iostream>
#include "Applications/ExampleApp.h"

int main()
{
	// TODO: Utilize args
	eRendererAPI rendererAPI = eRendererAPI::Vulkan;

	ExampleApp app = {};
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
