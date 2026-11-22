SlangVersion = "2026.19"

Path = {}
Path["Solution"] = "%{_WORKING_DIR}/"
Path["Target"] = path.join(Path.Solution, "Output/Binaries/%{cfg.buildcfg}/%{cfg.system}/")
Path["Obj"] = path.join(Path.Solution, "Output/Intermediate/%{cfg.buildcfg}/%{cfg.system}/%{prj.name}/")
Path["ThirdParty"] = path.join(Path.Solution, "Projects/ThirdParties/")
Path["Vulkan"] = os.getenv("VULKAN_SDK")
Path["Slang"] = path.join(Path.Solution, "External/slang-" .. SlangVersion .. "/")

Package = {}
Package["Dxc"] = "Microsoft.Direct3D.DXC:1.8.2505.32"

IncludeDir = {}
IncludeDir["GLFW"] = "%{wks.location}/Projects/ThirdParties/GLFW/include/"

workspace "BaambooRenderer"
	filename "Baamboo"
	architecture "x64"
	startproject "Application"
	configurations { "Debug", "Release" }
	exceptionhandling ("On")

	disablewarnings { "4819" }

include ("Projects/")
