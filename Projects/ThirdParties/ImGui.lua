project "ImGui"
	location "imgui"
	kind "StaticLib"
	language "C++"

	targetdir (Path.Target)
	objdir (Path.Obj)

	files {
		"%{prj.name}/*.h",
		"%{prj.name}/*.cpp",
		"%{prj.name}/misc/cpp/**.h",
		"%{prj.name}/misc/cpp/**.cpp",

		"%{prj.name}/backends/imgui_impl_glfw.h",
		"%{prj.name}/backends/imgui_impl_glfw.cpp",
		"%{prj.name}/backends/imgui_impl_dx12.h",
		"%{prj.name}/backends/imgui_impl_dx12.cpp",
		"%{prj.name}/backends/imgui_impl_vulkan.h",
		"%{prj.name}/backends/imgui_impl_vulkan.cpp",
	}

	includedirs { 
		"%{prj.name}/",
		"%{prj.name}/backends/",
		"%{IncludeDir.GLFW}/",
		"%{Path.Vulkan}/Include/", 
	}

	-- only be applied on windows platform
	filter "system:windows"
		systemversion "latest"

	filter "configurations:Debug"
		defines "_DEBUG"
		runtime "Debug"
		symbols "on"

	filter "configurations:Release"
		defines "NDEBUG"
		runtime "Release"
		optimize "on"

printf("ImGui generated!\n")