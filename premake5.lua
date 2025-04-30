Path = {}
Path["Solution"] = "%{_WORKING_DIR}/"
Path["Target"] = path.join(Path.Solution, "Output/Binaries/%{cfg.buildcfg}/%{cfg.system}/%{prj.name}/")
Path["Obj"] = path.join(Path.Solution, "Output/Intermediate/%{cfg.buildcfg}/%{cfg.system}/%{prj.name}/")
Path["ThirdParty"] = path.join(Path.Solution, "Projects/ThirdParties/")
Path["Vulkan"] = os.getenv("VULKAN_SDK")
Path["ShaderSrc"] = path.join(Path.Solution, "Assets/Shader/")
Path["Cso"] = path.join(Path.Solution, "Output/Shader/cso/")
Path["Spv"] = path.join(Path.Solution, "Output/Shader/spv")

Package = {}

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