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

	debugenvs { 
		"PATH=Projects/ThirdParties/assimp/bin/Release/;Output/Binaries/%{cfg.buildcfg}/%{cfg.system}/BaambooCommon;%PATH%"
	}

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
project "BaambooEngine"
	location "BaambooEngine"
	kind "StaticLib"
	language "C++"
	cppdialect "C++23"
	staticruntime "off"
	debugdir (Path.Solution)

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
		"%{prj.name}/**.cpp",
		"%{prj.name}/**.c",

		"%{Path.Solution}Projects/ThirdParties/imgui/*.h",
		"%{Path.Solution}Projects/ThirdParties/imgui/*.cpp",
		"%{Path.Solution}Projects/ThirdParties/imgui/misc/cpp/imgui_stdlib.h",
		"%{Path.Solution}Projects/ThirdParties/imgui/misc/cpp/imgui_stdlib.cpp",
		"%{Path.Solution}Projects/ThirdParties/imgui/backends/imgui_impl_glfw.h",
		"%{Path.Solution}Projects/ThirdParties/imgui/backends/imgui_impl_glfw.cpp",
	}

	includedirs {
		"%{prj.name}/",
		"%{Path.Solution}Projects/BaambooCommon",
		"%{Path.Solution}Projects/ThirdParties",
		"%{Path.Solution}Projects/ThirdParties/glm",
		"%{Path.Solution}Projects/ThirdParties/glfw/include",
		"%{Path.Solution}Projects/ThirdParties/imgui",
		"%{Path.Solution}Projects/ThirdParties/entt/single_include",
		"%{Path.Solution}Projects/ThirdParties/assimp/include",
		"%{Path.Solution}Projects/ThirdParties/magic_enum/include",
		"%{Path.Solution}Projects/ThirdParties/meshoptimizer/src",
	}

	libdirs {
		"%{Path.ThirdParty}/assimp/lib/Release",
		"%{Path.ThirdParty}/glfw/src/Release",
		"%{Path.Solution}Projects/ThirdParties/meshoptimizer/Release",
	}

	links {
		"BaambooCommon",
		"glfw3.lib",
		'assimp-vc143-mt.lib', 
		'meshoptimizer.lib', 
	}

	debugenvs { 
		"PATH=Projects/ThirdParties/assimp/bin/Release/;Output/Binaries/%{cfg.buildcfg}/%{cfg.system}/BaambooCommon;%PATH%"
	}

	filter { "files:ThirdParties/imgui/**.cpp" }
		flags "NoPCH"

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
		optimize "on"

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
		"%{Path.ShaderSrc}/HLSL/**.hlsl",

		"%{Path.Solution}Projects/ThirdParties/imgui/*.h",
		"%{Path.Solution}Projects/ThirdParties/imgui/*.cpp",
		"%{Path.Solution}Projects/ThirdParties/imgui/backends/imgui_impl_dx12.h",
		"%{Path.Solution}Projects/ThirdParties/imgui/backends/imgui_impl_dx12.cpp",
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
	}

	libdirs {
	}

	links {
		"BaambooCommon"
	}

	shadermodel ("6.6")
	shaderobjectfileoutput (path.join(Path.Cso, "%%(Filename).cso"))
	filter { "files:**VS.hlsl" }
		shadertype "Vertex"

	filter { "files:**PS.hlsl" }
		shadertype "Pixel"

	filter { "files:**GS.hlsl" }
		shadertype "Geometry"

	filter { "files:**DS.hlsl" }
		shadertype "Domain"

	filter { "files:**HS.hlsl" }
		shadertype "Hull"

	filter { "files:**CS.hlsl" }
		shadertype "Compute"

	filter { "files:**MS.hlsl" }
		shadertype "Mesh"

	filter { "files:**AS.hlsl" }
		shadertype "Amplification"

	filter { "files:Dx12Renderer/RenderDevice/D3D12MemoryAllocator/src/**.cpp" }
		flags "NoPCH"
	filter { "files:ThirdParties/imgui/**.cpp" }
		flags "NoPCH"

	filter "system:windows"
		systemversion "latest"
		defines { 
		}

		nuget { 
			"Microsoft.Direct3D.D3D12:1.618.2",
			"directxtk12_desktop_2019:2025.10.28.1",
			"directxtex_desktop_2019:2025.10.28.1",
			"Microsoft.Direct3D.DXC:1.8.2505.32",
		 }
    	-- Copy Agility SDK DLLs to output directory
    	postbuildcommands {
			'{MKDIR} "%{cfg.buildtarget.directory}D3D12"',
    	    '{COPYDIR} "%{_WORKING_DIR}//packages/Microsoft.Direct3D.D3D12.1.618.2/build/native/bin/" "%{cfg.buildtarget.directory}//D3D12"',
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


rule "VkShaderCompile"
	location "VkRenderer"
	display "Compiling Shader to Spv"
	fileextension { ".vert", ".frag", ".geom", ".hull", ".domain", ".task", ".mesh", ".comp" }

	buildmessage 'Compiling %(Filename)%(Extension) to Spv'
	buildcommands '$(VULKAN_SDK)/Bin/glslangValidator %(FullPath) -V -gVS -o %{Path.Spv}/%(Filename)%(Extension).spv'
	buildoutputs '%{Path.Spv}/%(Filename)%(Extension).spv'
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
	rules { "VkShaderCompile" }

	files {
		"%{prj.name}/**.h",
		"%{prj.name}/**.hpp",
		"%{prj.name}/**.cpp",
		"%{prj.name}/**.c",
		"%{prj.name}/**.def",
		"%{Path.ShaderSrc}/GLSL/**",

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
		links { "spirv-cross-cored.lib", }
		
	filter "configurations:Release"
		defines "NDEBUG"
		runtime "Release"
		optimize "on"
		links { "spirv-cross-core.lib", }



-- ThirdParties --
include ("ThirdParties/")