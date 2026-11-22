-- App
project "Application"
	location "Application"
	kind "ConsoleApp"
	language "C++"
	cppdialect "C++23"
	staticruntime "off"
	debugdir (Path.Solution)

	targetdir (Path.Target)
	objdir (Path.Obj)

	callingconvention ("FastCall")
	exceptionhandling ("Off")
	rtti ("Off")
	floatingpoint ("Fast")
	flags { "MultiProcessorCompile" }
	warnings ("High")
	exceptionhandling ("On")
	
	files {
		"%{prj.name}/**.h",
		"%{prj.name}/**.hpp",
		"%{prj.name}/**.cpp",
	}

	includedirs {
		"%{prj.name}/",
		"%{Path.Solution}Projects/BaambooCommon",
		"%{Path.Solution}Projects/BaambooEngine",
		"%{Path.Solution}Projects/ThirdParties",
		"%{Path.Solution}Projects/ThirdParties/glm",
		"%{Path.Solution}Projects/ThirdParties/glfw/include",
		"%{Path.Solution}Projects/ThirdParties/imgui",
		"%{Path.Solution}Projects/ThirdParties/entt/single_include",
		"%{Path.Solution}Projects/ThirdParties/assimp/include",
		"%{Path.Solution}Projects/ThirdParties/magic_enum/include",
		"%{Path.Solution}Projects/ThirdParties/meshoptimizer/src",
	}

	links {
		"BaambooEngine",
	}

	dependson { "Dx12Renderer", "VkRenderer" }

	filter 'system:windows'
		systemversion 'latest'

	filter 'configurations:Debug'
		defines '_DEBUG'
		runtime 'Debug'
		symbols 'on'

	filter 'configurations:Release'
		defines 'NDEBUG'
		runtime 'Release'
		optimize 'on'


-- BaambooEngine
local assimpName = (_ACTION == "vs2026" and "assimp-vc145-mt" or "assimp-vc143-mt")
project "BaambooEngine"
	location "BaambooEngine"
	kind "StaticLib"
	language "C++"
	cppdialect "C++23"
	staticruntime "off"

	targetdir (Path.Target)
	objdir (Path.Obj)

	pchheader "BaambooPch.h"
	pchsource "%{prj.name}/BaambooPch.cpp"

	callingconvention ("FastCall")
	exceptionhandling ("Off")
	rtti ("Off")
	floatingpoint ("Fast")
	flags { "MultiProcessorCompile" }
	warnings ("High")
	exceptionhandling ("On")

	files {
		"%{prj.name}/**.h",
		"%{prj.name}/**.hpp",
		"%{prj.name}/**.inl",
		"%{prj.name}/**.cpp",
		"%{prj.name}/**.c",

		"%{Path.Solution}Projects/ThirdParties/imgui/*.h",
		"%{Path.Solution}Projects/ThirdParties/imgui/*.cpp",
		"%{Path.Solution}Projects/ThirdParties/imgui/misc/cpp/imgui_stdlib.h",
		"%{Path.Solution}Projects/ThirdParties/imgui/misc/cpp/imgui_stdlib.cpp",
		"%{Path.Solution}Projects/ThirdParties/imgui/backends/imgui_impl_glfw.h",
		"%{Path.Solution}Projects/ThirdParties/imgui/backends/imgui_impl_glfw.cpp",
		"%{Path.ThirdParty}/assimp/bin/Release/" .. assimpName .. ".dll",
	}

	includedirs {
		"%{prj.name}/",
		"%{Path.Solution}Projects/BaambooCommon",
		"%{Path.Solution}Projects/BaambooShader",
		"%{Path.Solution}Projects/ThirdParties",
		"%{Path.Solution}Projects/ThirdParties/glm",
		"%{Path.Solution}Projects/ThirdParties/glfw/include",
		"%{Path.Solution}Projects/ThirdParties/imgui",
		"%{Path.Solution}Projects/ThirdParties/entt/single_include",
		"%{Path.Solution}Projects/ThirdParties/assimp/include",
		"%{Path.Solution}Projects/ThirdParties/magic_enum/include",
		"%{Path.Solution}Projects/ThirdParties/meshoptimizer/src",
		"%{Path.Solution}Projects/ThirdParties/json/single_include",
		"%{Path.Solution}Projects/ThirdParties/pugixml/src",
		"%{Path.Solution}Projects/ThirdParties/openvdb/nanovdb",
	}

	libdirs {
		"%{Path.ThirdParty}/assimp/lib/Release",
		"%{Path.ThirdParty}/glfw/src/Release",
		"%{Path.Solution}Projects/ThirdParties/meshoptimizer/Release",
	}

	links {
		"BaambooCommon",
		"BaambooShader",
		"glfw3.lib",
		assimpName .. ".lib",
		'meshoptimizer.lib',
	}

	filter { "files:ThirdParties/imgui/**.cpp" }
		flags "NoPCH"
	filter { "files:**.dll" }
		buildaction "Copy"

	filter 'system:windows'
		systemversion 'latest'

	filter 'configurations:Debug'
		defines '_DEBUG'
		runtime 'Debug'
		symbols 'on'

	filter 'configurations:Release'
		defines 'NDEBUG'
		runtime 'Release'
		optimize 'on'


-- BaambooShader: the only module that sees Slang. It owns the shader toolchain's runtime files.
project "BaambooShader"
	location "BaambooShader"
	kind "StaticLib"
	language "C++"
	cppdialect "C++23"
	staticruntime "off"

	targetdir (Path.Target)
	objdir (Path.Obj)

	callingconvention ("FastCall")
	exceptionhandling ("Off")
	rtti ("Off")
	floatingpoint ("Fast")
	flags { "MultiProcessorCompile" }
	warnings ("High")
	exceptionhandling ("On")

	files {
		"%{prj.name}/**.h",
		"%{prj.name}/**.hpp",
		"%{prj.name}/**.cpp",

		-- Slang runtime, copied next to the exe. slang-glslang is also Slang's SPIR-V optimizer.
		"%{Path.Slang}/bin/slang-compiler.dll",
		"%{Path.Slang}/bin/slang-glslang.dll",
		"%{Path.Slang}/bin/slang-glsl-module.dll",
	}

	includedirs {
		"%{prj.name}/",
		"%{Path.Solution}Projects/BaambooCommon",
		"%{Path.Solution}Projects/ThirdParties/glm",
		"%{Path.Slang}/include",
	}

	libdirs {
		"%{Path.Slang}/lib",
	}

	links {
		"slang-compiler.lib",
	}

	filter { "files:**.dll" }
		buildaction "Copy"

	filter 'system:windows'
		systemversion 'latest'

		-- DXC behind Slang's DXIL target. The package copies dxcompiler.dll and dxil.dll next to the exe.
		nuget { Package.Dxc }

	filter 'configurations:Debug'
		defines '_DEBUG'
		runtime 'Debug'
		symbols 'on'

	filter 'configurations:Release'
		defines 'NDEBUG'
		runtime 'Release'
		optimize 'on'


-- BaambooCommon
project "BaambooCommon"
	location "BaambooCommon"
	kind "SharedLib"
	language "C++"
	cppdialect "C++23"
	staticruntime "off"

	targetdir (Path.Target)
	objdir (Path.Obj)

	callingconvention ("FastCall")
	exceptionhandling ("Off")
	rtti ("Off")
	floatingpoint ("Fast")
	flags { "MultiProcessorCompile" }
	warnings ("High")
	exceptionhandling ("On")

	files {
		"%{prj.name}/**.h",
		"%{prj.name}/**.hpp",
		"%{prj.name}/**.cpp"
	}

	includedirs {
		"%{prj.name}/",
		"%{Path.ThirdParty}/glm",
	}

	defines {
		"BB_COMMON_DLL"
	}

	filter "system:windows"
		systemversion "latest"

	filter "configurations:Debug"
		defines "_DEBUG"
		runtime "Debug"
		optimize "off"

	filter "configurations:Release"
		defines "NDEBUG"
		runtime "Release"
		optimize "on"


-- Renderers
project "Dx12Renderer"
	location "Dx12Renderer"
	kind "SharedLib"
	language "C++"
	cppdialect "C++23"
	staticruntime "off"
	debugdir (Path.Solution)

	targetdir (Path.Target)
	objdir (Path.Obj)

	pchheader "RendererPch.h"
	pchsource "%{prj.name}/RendererPch.cpp"

	callingconvention ("FastCall")
	exceptionhandling ("Off")
	rtti ("Off")
	floatingpoint ("Fast")
	flags { "MultiProcessorCompile" }
	warnings ("High")
	exceptionhandling ("On")

	files {
		"%{prj.name}/**.h",
		"%{prj.name}/**.hpp",
		"%{prj.name}/**.cpp",
		"%{prj.name}/**.c",
		"%{prj.name}/**.def",

		"%{Path.Solution}Projects/ThirdParties/imgui/*.h",
		"%{Path.Solution}Projects/ThirdParties/imgui/*.cpp",
		"%{Path.Solution}Projects/ThirdParties/imgui/backends/imgui_impl_dx12.h",
		"%{Path.Solution}Projects/ThirdParties/imgui/backends/imgui_impl_dx12.cpp",
		"%{Path.Solution}Projects/ThirdParties/tinyexr/deps/miniz/miniz.h",
		"%{Path.Solution}Projects/ThirdParties/tinyexr/deps/miniz/miniz.c",
	}
	removefiles {
		"%{prj.name}/RenderDevice/D3D12MemoryAllocator/src/D3D12Sample.cpp",
		"%{prj.name}/RenderDevice/D3D12MemoryAllocator/src/Tests.h",
		"%{prj.name}/RenderDevice/D3D12MemoryAllocator/src/Tests.cpp",
	}

	includedirs {
		"%{prj.name}/",
		"%{prj.name}/RenderDevice/D3D12MemoryAllocator/include/",
		"%{Path.Solution}Projects/BaambooCommon",
		"%{Path.Solution}Projects/BaambooEngine",
		"%{Path.Solution}Projects/ThirdParties",
		"%{Path.Solution}Projects/ThirdParties/glm",
		"%{Path.Solution}Projects/ThirdParties/glfw/include",
		"%{Path.Solution}Projects/ThirdParties/imgui",
		"%{Path.Solution}Projects/ThirdParties/tinyexr/deps/miniz",  -- miniz.h for tinyexr
	}

	libdirs {
	}

	links {
		"BaambooCommon"
	}

	filter { "files:Dx12Renderer/RenderDevice/D3D12MemoryAllocator/src/**.cpp" }
		flags "NoPCH"
	filter { "files:Dx12Renderer/RenderDevice/Dx12EXRSupport.cpp" }
		flags "NoPCH"
	filter { "files:ThirdParties/imgui/**.cpp" }
		flags "NoPCH"
	filter { "files:ThirdParties/tinyexr/deps/miniz/**.c" }
		flags "NoPCH"

	filter "system:windows"
		systemversion "latest"
		defines {
		}

		nuget {
			"Microsoft.Direct3D.D3D12:1.618.5",
			"directxtk12_desktop_win10:2026.4.1.1",
			"directxtex_desktop_win10:2026.4.1.1",
			"WinPixEventRuntime:1.0.240308001"
		 }
    	-- Copy Agility SDK DLLs to output directory
    	postbuildcommands {
			'{MKDIR} "%{cfg.buildtarget.directory}D3D12"',
    	    '{COPYDIR} "%{_WORKING_DIR}//packages/Microsoft.Direct3D.D3D12.1.618.5/build/native/bin/" "%{cfg.buildtarget.directory}//D3D12"',
    	}

	filter "system:linux"
		systemversion "latest"
		defines {}

	filter "configurations:Debug"
		defines "_DEBUG"
		runtime "Debug"
		symbols "on"
		links {  }
		
	filter "configurations:Release"
		defines "NDEBUG"
		runtime "Release"
		optimize "on"
		links {  }


project "VkRenderer"
	location "VkRenderer"
	kind "SharedLib"
	language "C++"
	cppdialect "C++23"
	staticruntime "off"
	debugdir (Path.Solution)

	targetdir (Path.Target)
	objdir (Path.Obj)

	pchheader "RendererPch.h"
	pchsource "%{prj.name}/RendererPch.cpp"

	callingconvention ("FastCall")
	exceptionhandling ("Off")
	rtti ("Off")
	floatingpoint ("Fast")
	flags { "MultiProcessorCompile" }
	warnings ("High")
	exceptionhandling ("On")

	files {
		"%{prj.name}/**.h",
		"%{prj.name}/**.hpp",
		"%{prj.name}/**.cpp",
		"%{prj.name}/**.c",
		"%{prj.name}/**.def",

		"%{Path.Solution}Projects/ThirdParties/imgui/*.h",
		"%{Path.Solution}Projects/ThirdParties/imgui/*.cpp",
		"%{Path.Solution}Projects/ThirdParties/imgui/backends/imgui_impl_vulkan.h",
		"%{Path.Solution}Projects/ThirdParties/imgui/backends/imgui_impl_vulkan.cpp",
	}
	removefiles { 
		"%{prj.name}/RenderDevice/VulkanMemoryAllocator/src/**",
	}

	includedirs {
		"%{prj.name}/",
		"%{prj.name}/RenderDevice/VulkanMemoryAllocator/include/",
		"%{Path.Solution}Projects/BaambooCommon",
		"%{Path.Solution}Projects/BaambooEngine",
		"%{Path.Solution}Projects/ThirdParties",
		"%{Path.Solution}Projects/ThirdParties/glm",
		"%{Path.Solution}Projects/ThirdParties/glfw/include",
		"%{Path.Solution}Projects/ThirdParties/imgui",
		"%{Path.Solution}Projects/ThirdParties/gli/",
		
		'%{Path.Vulkan}/Include/',
	}

	libdirs {
		"%{Path.Vulkan}/Lib/",
	}

	links {
		"vulkan-1.lib",
		"BaambooCommon",
	}

	filter { "files:ThirdParties/imgui/**.cpp" }
		flags "NoPCH"

	filter "configurations:Debug"
		defines "_DEBUG"
		runtime "Debug"
		symbols "on"
		
	filter "configurations:Release"
		defines "NDEBUG"
		runtime "Release"
		optimize "on"



-- ThirdParties --
include ("ThirdParties/")
