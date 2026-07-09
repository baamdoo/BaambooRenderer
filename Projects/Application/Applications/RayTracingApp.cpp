#include "RayTracingApp.h"

#include "BaambooCore/Common.h"
#include "BaambooCore/Window.h"
#include "BaambooCore/Input.hpp"

#include "BaambooScene/Entity.h"
#include "BaambooScene/Components.h"
#include "BaambooScene/RenderNodes/SkyboxNode.h"
#include "BaambooScene/RenderNodes/RaytracingNode.h"
#include "BaambooScene/RenderNodes/PathTracerNode.h"
#include "BaambooScene/RenderNodes/PostProcessNode.h"

#include <glm/gtc/type_ptr.hpp>
#include <imgui/backends/imgui_impl_glfw.h>

#include <fstream>
#include <sstream>
#include <unordered_map>

using namespace baamboo;

namespace
{

constexpr u32 kPathTracerPrincipledMaterialType = 5u;

struct GalleryMaterialEntry
{
	std::string path;
	std::string name;
	float4 tint = float4(1.0f);
	float metallic = 0.0f;
	float roughness = 1.0f;
	float ior = 1.5f;
	float transmission = 0.0f;
	float3 specularColor = float3(1.0f);
	float specularStrength = 1.0f;
	float3 emissionColor = float3(1.0f);
	float emissivePower = 0.0f;
	std::string albedoTex;
	float alphaCutoff = 0.0f;
	bool bFaceNormals = false;
};

struct GalleryAreaLightEntry
{
	std::string name;
	float3 position = float3(0.0f);
	float3 rotation = float3(0.0f);
	float3 scale = float3(1.0f);
	float3 color = float3(1.0f);
	float luminousFluxLm = 0.0f;
};

struct GallerySceneData
{
	bool bValid = false;
	u32 width = 512u;
	u32 height = 512u;
	float3 cameraPosition = float3(0.0f, 1.0f, 3.5f);
	float3 cameraTarget = float3(0.0f, 1.0f, 0.0f);
	float3 cameraUp = float3(0.0f, 1.0f, 0.0f);
	float cameraFovY = 40.0f;
	u32 samplesPerFrame = 8u;
	u32 dumpTargetSamples = 4096u;
	u32 maxDepth = 12u;
	std::string environmentMapPath;
	std::vector< GalleryMaterialEntry > meshes;
	std::vector< GalleryAreaLightEntry > areaLights;
};

bool IsGalleryScene(std::string_view sceneName)
{
	return sceneName.starts_with("gallery_");
}

fs::path GalleryManifestPath(std::string_view sceneName)
{
	return ASSET_PATH / "Generated" / std::string(sceneName) / "scene.baamboo";
}

std::vector< std::string > SplitLine(const std::string& line)
{
	std::istringstream iss(line);
	std::vector< std::string > tokens;
	std::string token;
	while (iss >> token)
		tokens.push_back(token);
	return tokens;
}

std::unordered_map< std::string, std::string > ParseKeyValues(const std::vector< std::string >& tokens, size_t first)
{
	std::unordered_map< std::string, std::string > values;
	for (size_t i = first; i < tokens.size(); ++i)
	{
		const auto pos = tokens[i].find('=');
		if (pos == std::string::npos)
			continue;
		values.emplace(tokens[i].substr(0, pos), tokens[i].substr(pos + 1));
	}
	return values;
}

float ParseFloat(const std::unordered_map< std::string, std::string >& values, const char* key, float fallback)
{
	if (auto it = values.find(key); it != values.end())
		return std::stof(it->second);
	return fallback;
}

u32 ParseU32(const std::unordered_map< std::string, std::string >& values, const char* key, u32 fallback)
{
	if (auto it = values.find(key); it != values.end())
		return static_cast< u32 >(std::stoul(it->second));
	return fallback;
}

std::vector< float > ParseFloatList(std::string_view value)
{
	std::vector< float > out;
	std::string text(value);
	std::string part;
	std::stringstream ss(text);
	while (std::getline(ss, part, ','))
	{
		if (!part.empty())
			out.push_back(std::stof(part));
	}
	return out;
}

float3 ParseFloat3Value(const std::unordered_map< std::string, std::string >& values, const char* key, const float3& fallback)
{
	if (auto it = values.find(key); it != values.end())
	{
		auto parsed = ParseFloatList(it->second);
		if (parsed.size() >= 3)
			return float3(parsed[0], parsed[1], parsed[2]);
	}
	return fallback;
}

float4 ParseFloat4Value(const std::unordered_map< std::string, std::string >& values, const char* key, const float4& fallback)
{
	if (auto it = values.find(key); it != values.end())
	{
		auto parsed = ParseFloatList(it->second);
		if (parsed.size() >= 4)
			return float4(parsed[0], parsed[1], parsed[2], parsed[3]);
		if (parsed.size() >= 3)
			return float4(parsed[0], parsed[1], parsed[2], 1.0f);
	}
	return fallback;
}

std::string AssetRelativeToString(const std::string& relativePath)
{
	if (relativePath.empty() || relativePath == "-")
		return std::string();
	return (ASSET_PATH / fs::path(relativePath)).string();
}

GallerySceneData LoadGallerySceneManifest(std::string_view sceneName)
{
	GallerySceneData data = {};
	std::ifstream in(GalleryManifestPath(sceneName));
	if (!in.is_open())
		return data;

	data.bValid = true;
	std::string line;
	while (std::getline(in, line))
	{
		if (line.empty() || line[0] == '#')
			continue;
		auto tokens = SplitLine(line);
		if (tokens.empty())
			continue;

		if (tokens[0] == "resolution" && tokens.size() >= 3)
		{
			data.width = static_cast< u32 >(std::stoul(tokens[1]));
			data.height = static_cast< u32 >(std::stoul(tokens[2]));
		}
		else if (tokens[0] == "camera")
		{
			auto values = ParseKeyValues(tokens, 1);
			data.cameraPosition = ParseFloat3Value(values, "pos", data.cameraPosition);
			data.cameraTarget = ParseFloat3Value(values, "target", data.cameraTarget);
			data.cameraUp = ParseFloat3Value(values, "up", data.cameraUp);
			data.cameraFovY = ParseFloat(values, "fovY", data.cameraFovY);
		}
		else if (tokens[0] == "pathtracer")
		{
			auto values = ParseKeyValues(tokens, 1);
			data.samplesPerFrame = ParseU32(values, "spp", data.samplesPerFrame);
			data.dumpTargetSamples = ParseU32(values, "dumpSamples", data.dumpTargetSamples);
			data.maxDepth = ParseU32(values, "maxDepth", data.maxDepth);
		}
		else if (tokens[0] == "environment")
		{
			auto values = ParseKeyValues(tokens, 1);
			if (auto it = values.find("path"); it != values.end())
				data.environmentMapPath = AssetRelativeToString(it->second);
		}
		else if (tokens[0] == "mesh")
		{
			auto values = ParseKeyValues(tokens, 1);
			GalleryMaterialEntry entry = {};
			if (auto it = values.find("path"); it != values.end())
				entry.path = AssetRelativeToString(it->second);
			if (auto it = values.find("name"); it != values.end())
				entry.name = it->second;
			entry.bFaceNormals = ParseU32(values, "faceNormals", 0u) != 0u;
			entry.tint = ParseFloat4Value(values, "tint", entry.tint);
			entry.metallic = ParseFloat(values, "metallic", entry.metallic);
			entry.roughness = ParseFloat(values, "roughness", entry.roughness);
			entry.ior = ParseFloat(values, "ior", entry.ior);
			entry.transmission = ParseFloat(values, "transmission", entry.transmission);
			entry.specularColor = ParseFloat3Value(values, "specularColor", entry.specularColor);
			entry.specularStrength = ParseFloat(values, "specularStrength", entry.specularStrength);
			entry.emissionColor = ParseFloat3Value(values, "emissionColor", entry.emissionColor);
			entry.emissivePower = ParseFloat(values, "emissivePower", entry.emissivePower);
			entry.alphaCutoff = ParseFloat(values, "alphaCutoff", entry.alphaCutoff);
			if (auto it = values.find("albedoTex"); it != values.end())
				entry.albedoTex = AssetRelativeToString(it->second);
			if (!entry.path.empty())
				data.meshes.push_back(entry);
		}
		else if (tokens[0] == "areaLight")
		{
			auto values = ParseKeyValues(tokens, 1);
			GalleryAreaLightEntry light = {};
			if (auto it = values.find("name"); it != values.end())
				light.name = it->second;
			light.position = ParseFloat3Value(values, "position", light.position);
			light.rotation = ParseFloat3Value(values, "rotation", light.rotation);
			light.scale = ParseFloat3Value(values, "scale", light.scale);
			light.color = ParseFloat3Value(values, "color", light.color);
			light.luminousFluxLm = ParseFloat(values, "flux", light.luminousFluxLm);
			data.areaLights.push_back(light);
		}
	}
	return data;
}
bool IsPrincipledMaterialTestScene(std::string_view sceneName)
{
	return sceneName == "cornell_principled" || sceneName == "cornell_principled_diffuse" ||
		sceneName == "cornell_principled_specular" || sceneName == "cornell_principled_clearcoat" ||
		sceneName == "cornell_principled_sheen";
}

bool IsEnvironmentMapTestScene(std::string_view sceneName)
{
	return sceneName == "room_envmap" || sceneName == "room_envmap_importance";
}

bool IsComplexRoomTestScene(std::string_view sceneName)
{
	return sceneName == "complex_room" || sceneName == "complex_room_envmap_mis";
}

bool IsSupportedPathTracerScene(std::string_view sceneName)
{
	return sceneName == "cornell_box" || sceneName == "cornell_open" || sceneName == "cornell_sphere_light" || sceneName == "cornell_directional_light" || sceneName == "cornell_disk_light" || sceneName == "cornell_spot_light" || sceneName == "cornell_tube_light" || sceneName == "cornell_many_lights" || sceneName == "cornell_textured" || sceneName == "cornell_material_maps" || sceneName == "cornell_normal_map" ||
		sceneName == "cornell_mesh" || sceneName == "cornell_transform" || sceneName == "cornell_transform_rot" ||
		sceneName == "cornell_transform_model" || IsEnvironmentMapTestScene(sceneName) || sceneName == "dining_room" || IsComplexRoomTestScene(sceneName) ||
		sceneName == "cornell_box_conductor" || sceneName == "cornell_box_conductor_smooth" || sceneName == "cornell_anisotropic_conductor" ||
		sceneName == "cornell_box_mixed_metallic" || sceneName == "cornell_box_opaque_dielectric" || sceneName == "cornell_box_mixed_transmission" ||
		sceneName == "cornell_box_dielectric" || sceneName == "cornell_box_dielectric_smooth" ||
		sceneName == "cornell_principled_glass" || IsPrincipledMaterialTestScene(sceneName) || IsGalleryScene(sceneName);
}

} // namespace

void RayTracingApp::Initialize(eRendererAPI api)
{
	m_DeviceSettings.bRaytracing = true;
	m_DeviceSettings.bMeshShader = true;

	Super::Initialize(api);

	float cameraFovY = IsEnvironmentMapTestScene(m_PathTracerReferenceScene) ? 45.0f : 40.0f;
	if (IsGalleryScene(m_PathTracerReferenceScene))
	{
		const auto gallery = LoadGallerySceneManifest(m_PathTracerReferenceScene);
		if (gallery.bValid)
		{
			m_CameraController.SetLookAt(gallery.cameraPosition, gallery.cameraTarget, gallery.cameraUp);
			cameraFovY = gallery.cameraFovY;
		}
		else
		{
			m_CameraController.SetLookAt(float3(0.0f, 1.0f, 3.5f), float3(0.0f, 1.0f, 0.0f));
		}
	}
	else if (IsEnvironmentMapTestScene(m_PathTracerReferenceScene))
	{
		m_CameraController.SetLookAt(float3(2.6f, -2.6f, 1.7f), float3(0.0f, 0.0f, 0.55f), float3(0.0f, 0.0f, 1.0f));
	}
	else
	{
		m_CameraController.SetLookAt(float3(0.0f, 1.0f, 3.5f), float3(0.0f, 1.0f, 0.0f));
	}

	m_pCamera = new EditorCamera(m_CameraController, m_pWindow->Width(), m_pWindow->Height());
	m_pCamera->fov   = cameraFovY;
	m_pCamera->zNear = 0.1f;
	m_pCamera->zFar  = 100.0f;
}

void RayTracingApp::Update(float dt)
{
	Super::Update(dt);

	m_CameraController.Update(dt);

	if (m_bExitAfterAOVDump && m_pPathTracerNode.valid())
	{
		auto pPathTracerNode = m_pPathTracerNode.lock();
		if (pPathTracerNode && pPathTracerNode->IsAOVDumpComplete())
			glfwSetWindowShouldClose(m_pWindow->Handle(), GLFW_TRUE);
	}
}

void RayTracingApp::Release()
{
	m_CameraController.Reset();
	m_pPathTracerNode = Weak< PathTracerNode >();

	Super::Release();
}

void RayTracingApp::ConfigurePathTracerAutomation(bool bDumpAOV, bool bExitAfterDump, std::string_view referenceSceneName)
{
	m_bAutoDumpAOV      = bDumpAOV;
	m_bExitAfterAOVDump = bExitAfterDump;
	m_PathTracerReferenceScene = IsSupportedPathTracerScene(referenceSceneName) ? std::string(referenceSceneName) : "cornell_box";
}

bool RayTracingApp::InitWindow()
{
	// **
	// Create window
	// **
	u32 windowWidth = 512u;
	u32 windowHeight = 512u;
	if (IsGalleryScene(m_PathTracerReferenceScene))
	{
		const auto gallery = LoadGallerySceneManifest(m_PathTracerReferenceScene);
		if (gallery.bValid)
		{
			windowWidth = gallery.width;
			windowHeight = gallery.height;
		}
	}
	WindowDescriptor windowDesc = { .width = static_cast< i32 >(windowWidth), .height = static_cast< i32 >(windowHeight), .numDesiredImages = 3, .bVSync = false };
	m_pWindow = new Window(windowDesc);


	// **
	// Set callbacks
	// **
	glfwSetWindowUserPointer(m_pWindow->Handle(), this);
	m_pWindow->SetKeyCallback([](GLFWwindow* window, i32 key, i32 scancode, i32 action, i32 mods)
		{
			ImGui_ImplGlfw_KeyCallback(window, key, scancode, action, mods);

			ImGuiIO& io = ImGui::GetIO();
			if (!io.WantCaptureKeyboard)
			{
				auto app = reinterpret_cast<RayTracingApp*>(glfwGetWindowUserPointer(window));
				if (app)
				{
					bool bPressed = action != GLFW_RELEASE;
					Input::Inst()->UpdateKey(key, bPressed);

					switch (key)
					{
					case GLFW_KEY_ESCAPE:
						if (bPressed)
							glfwSetWindowShouldClose(window, GLFW_TRUE);
						break;

					default:
						break;
					}
				}
			}
		});

	m_pWindow->SetMouseButtonCallback([](GLFWwindow* window, i32 button, i32 action, i32 mods)
		{
			ImGui_ImplGlfw_MouseButtonCallback(window, button, action, mods);

			ImGuiIO& io = ImGui::GetIO();
			if (!io.WantCaptureMouse)
			{
				printf("MouseClicked on scene!\n");

				bool bPressed = action != GLFW_RELEASE;
				Input::Inst()->UpdateMouse(button, bPressed);
			}
			else
			{
				printf("MouseClicked on ui!\n");
			}
		});

	m_pWindow->SetMouseMoveCallback([](GLFWwindow* window, double xpos, double ypos)
		{
			ImGui_ImplGlfw_CursorPosCallback(window, xpos, ypos);

			ImGuiIO& io = ImGui::GetIO();
			if (!io.WantCaptureMouse)
			{
				Input::Inst()->UpdateMousePosition(static_cast<float>(xpos), static_cast<float>(ypos));
			}
		});

	m_pWindow->SetMouseWheelCallback([](GLFWwindow* window, double xoffset, double yoffset)
		{
			ImGui_ImplGlfw_ScrollCallback(window, xoffset, yoffset);

			ImGuiIO& io = ImGui::GetIO();
			if (!io.WantCaptureMouse)
			{
				Input::Inst()->UpdateMouseWheel(static_cast<float>(yoffset));
			}
		});

	m_pWindow->SetResizeCallback([](GLFWwindow* window, i32 width, i32 height)
		{
			auto app = reinterpret_cast<RayTracingApp*>(glfwGetWindowUserPointer(window));
			if (app)
			{
				app->m_bWindowResized = true;
				app->m_ResizeWidth    = width;
				app->m_ResizeHeight   = height;
			}
		});

	m_pWindow->SetIconifyCallback([](GLFWwindow* window, i32 iconified)
		{
			auto app = reinterpret_cast<RayTracingApp*>(glfwGetWindowUserPointer(window));
			if (app)
			{
				app->m_bWindowResized = true;
				app->m_ResizeWidth    = iconified ? 0 : app->m_pWindow->Width();
				app->m_ResizeHeight   = iconified ? 0 : app->m_pWindow->Height();
			}
		});

	m_pWindow->SetCharCallback([](GLFWwindow* window, u32 c)
		{
			ImGui_ImplGlfw_CharCallback(window, c);
		});

	return true;
}

bool RayTracingApp::LoadScene()
{
	m_pScene = new Scene("RaytracingTestScene");

	ConfigureRenderGraph();
	ConfigureSceneObjects();

	return m_pScene != nullptr;
}

void RayTracingApp::DrawUI()
{
	Super::DrawUI();

	ImGui::Begin("Editor Camera");
	{
		if (ImGui::CollapsingHeader("Transform"))
		{
			auto& transform = m_CameraController.GetTransform();

			ImGui::Text("Position");
			ImGui::DragFloat3("##Position", glm::value_ptr(transform.position), 0.1f, 0.0f, 0.0f, "%.1f");

			ImGui::Text("Rotation");
			ImGui::DragFloat3("##Rotation", glm::value_ptr(transform.rotation), 0.1f, 0.0f, 0.0f, "%.1f");
		}

		if (ImGui::CollapsingHeader("Camera"))
		{
			float width = (ImGui::GetWindowWidth() - ImGui::GetStyle().ItemSpacing.x);

			ImGui::PushItemWidth(width * 0.3f);
			ImGui::Text("ClippingRange");
			ImGui::InputFloat("##ClipNear", &m_pCamera->zNear, 0, 0, "%.2f");

			ImGui::PushItemWidth(width * 0.7f);
			ImGui::SameLine();
			ImGui::InputFloat("##ClipFar", &m_pCamera->zFar, 0, 0, "%.2f");

			ImGui::Text("FoV");
			ImGui::DragFloat("##FoV", &m_pCamera->fov, 0.1f, 1.0f, 90.0f, "%.1f");
		}

		if (ImGui::CollapsingHeader("Controller"))
		{
			auto& cameraConfig = m_CameraController.config;

			ImGui::Text("Rotation Acceleration");
			ImGui::DragFloat("##RAcceleration", &cameraConfig.rotationAcceleration, 10.0f, 10.0f, 300.0f, "%.1f");

			ImGui::Text("Rotation Damping");
			ImGui::DragFloat("##RDamping", &cameraConfig.rotationDamping, 0.1f, 1.0f, 10.0f, "%.1f");

			ImGui::Text("Move Acceleration");
			ImGui::DragFloat("##MAcceleration", &cameraConfig.moveAcceleration, 1.0f, 10.0f, 100.0f, "%.1f");

			ImGui::Text("Move Damping");
			ImGui::DragFloat("##MDamping", &cameraConfig.moveDamping, 0.1f, 1.0f, 10.0f, "%.1f");

			ImGui::Text("Boosting Speed");
			ImGui::DragFloat("##Boosting", &cameraConfig.boostingSpeed, 1.0f, 1.0f, 100.0f, "%.1f");

			static eWorldDistanceScaleType cameraScaleCache = eWorldDistanceScaleType::M;
			if (ImGui::BeginCombo("##Scale", "Movement Scale"))
			{
				if (ImGui::Selectable("cm", cameraScaleCache == eWorldDistanceScaleType::CM))
				{
					cameraConfig.movementScale = 0.01f;
					cameraScaleCache = eWorldDistanceScaleType::CM;
				}
				if (ImGui::Selectable("m", cameraScaleCache == eWorldDistanceScaleType::M))
				{
					cameraConfig.movementScale = 1.0f;
					cameraScaleCache = eWorldDistanceScaleType::M;
				}
				if (ImGui::Selectable("km", cameraScaleCache == eWorldDistanceScaleType::KM))
				{
					cameraConfig.movementScale = 1000.0f;
					cameraScaleCache = eWorldDistanceScaleType::KM;
				}

				ImGui::EndCombo();
			}
		}
	}
	ImGui::End();
}

void RayTracingApp::ConfigureRenderGraph()
{
	m_pScene->AddRenderNode(MakeArc< StaticSkyboxNode >(*m_pRendererBackend->GetDevice()));
	auto pPathTracerNode = MakeArc< PathTracerNode >(*m_pRendererBackend->GetDevice());
	if (IsGalleryScene(m_PathTracerReferenceScene))
	{
		const auto gallery = LoadGallerySceneManifest(m_PathTracerReferenceScene);
		if (gallery.bValid)
			pPathTracerNode->ConfigureReferenceScene(m_PathTracerReferenceScene, float3(0.0f), gallery.samplesPerFrame, gallery.dumpTargetSamples, gallery.maxDepth, gallery.environmentMapPath);
		else
			pPathTracerNode->ConfigureReferenceScene(m_PathTracerReferenceScene, float3(0.0f), 4u, 1024u, 12u);
	}
	else if (m_PathTracerReferenceScene == "room_envmap_importance")
		pPathTracerNode->ConfigureReferenceScene(m_PathTracerReferenceScene, float3(0.0f), 4u, 4096u, 1u, (ASSET_PATH / "Texture" / "_synthetic" / "sky_pretty.exr").string());
	else if (m_PathTracerReferenceScene == "room_envmap")
		pPathTracerNode->ConfigureReferenceScene(m_PathTracerReferenceScene, float3(0.0f), 4u, 1024u, 8u, (ASSET_PATH / "Texture" / "_synthetic" / "sky_pretty.exr").string());
	else if (m_PathTracerReferenceScene == "dining_room")
		pPathTracerNode->ConfigureReferenceScene(m_PathTracerReferenceScene, float3(0.0f), 8u, 4096u, 12u);
	else if (m_PathTracerReferenceScene == "complex_room_envmap_mis")
		pPathTracerNode->ConfigureReferenceScene(m_PathTracerReferenceScene, float3(0.0f), 8u, 4096u, 12u, (ASSET_PATH / "Texture" / "_synthetic" / "sky_pretty.exr").string());
	else if (m_PathTracerReferenceScene == "complex_room")
		pPathTracerNode->ConfigureReferenceScene(m_PathTracerReferenceScene, float3(0.12f, 0.12f, 0.16f), 8u, 4096u, 12u);
	else if (m_PathTracerReferenceScene == "cornell_open")
		pPathTracerNode->ConfigureReferenceScene(m_PathTracerReferenceScene, float3(1.0f), 4u, 1024u);
	else if (m_PathTracerReferenceScene == "cornell_many_lights")
		pPathTracerNode->ConfigureReferenceScene(m_PathTracerReferenceScene, float3(0.0f), 4u, 4096u, 1u);
	else if (m_PathTracerReferenceScene == "cornell_tube_light")
		pPathTracerNode->ConfigureReferenceScene(m_PathTracerReferenceScene, float3(0.0f), 4u, 4096u, 1u);
	else if (m_PathTracerReferenceScene == "cornell_directional_light" || m_PathTracerReferenceScene == "cornell_disk_light" || m_PathTracerReferenceScene == "cornell_spot_light")
		pPathTracerNode->ConfigureReferenceScene(m_PathTracerReferenceScene, float3(0.0f), 4u, 1024u, 1u);
	else if (m_PathTracerReferenceScene == "cornell_anisotropic_conductor")
		pPathTracerNode->ConfigureReferenceScene(m_PathTracerReferenceScene, float3(0.0f), 8u, 4096u, 12u);
	else if (m_PathTracerReferenceScene == "cornell_box_opaque_dielectric")
		pPathTracerNode->ConfigureReferenceScene(m_PathTracerReferenceScene, float3(0.0f), 8u, 4096u);
	else if (m_PathTracerReferenceScene == "cornell_textured" || m_PathTracerReferenceScene == "cornell_mesh" ||
		m_PathTracerReferenceScene == "cornell_transform" || m_PathTracerReferenceScene == "cornell_transform_rot" ||
		m_PathTracerReferenceScene == "cornell_transform_model" || m_PathTracerReferenceScene == "cornell_sphere_light" ||
		m_PathTracerReferenceScene == "cornell_box_conductor" || m_PathTracerReferenceScene == "cornell_box_mixed_metallic" ||
		m_PathTracerReferenceScene == "cornell_material_maps" || m_PathTracerReferenceScene == "cornell_normal_map")
		pPathTracerNode->ConfigureReferenceScene(m_PathTracerReferenceScene, float3(0.0f), 4u, 1024u);
	else if (m_PathTracerReferenceScene == "cornell_box_conductor_smooth")
		pPathTracerNode->ConfigureReferenceScene(m_PathTracerReferenceScene, float3(0.0f), 8u, 4096u, 16u);
	else if (m_PathTracerReferenceScene == "cornell_box_mixed_transmission")
		pPathTracerNode->ConfigureReferenceScene(m_PathTracerReferenceScene, float3(0.0f), 8u, 4096u, 12u);
	else if (IsPrincipledMaterialTestScene(m_PathTracerReferenceScene))
		pPathTracerNode->ConfigureReferenceScene(m_PathTracerReferenceScene, float3(0.0f), 8u, 4096u, 12u);
	else if (m_PathTracerReferenceScene == "cornell_box_dielectric" || m_PathTracerReferenceScene == "cornell_principled_glass")
		pPathTracerNode->ConfigureReferenceScene(m_PathTracerReferenceScene, float3(0.0f), 8u, 2048u, 12u);
	else if (m_PathTracerReferenceScene == "cornell_box_dielectric_smooth")
		pPathTracerNode->ConfigureReferenceScene(m_PathTracerReferenceScene, float3(0.0f), 8u, 4096u, 16u);
	else
		pPathTracerNode->ConfigureReferenceScene("cornell_box", float3(0.0f), 1u, 128u);

	m_pPathTracerNode = pPathTracerNode;
	if (m_bAutoDumpAOV)
		pPathTracerNode->RequestAOVDump();
	m_pScene->AddRenderNode(pPathTracerNode);
	m_pScene->AddRenderNode(MakeArc< PostProcessNode >(*m_pRendererBackend->GetDevice()));
}

void RayTracingApp::ConfigureSceneObjects()
{
	const auto createPostProcessVolume = [this]()
		{
			auto  volume = m_pScene->CreateEntity("PostProcessVolume");
			auto& pp     = volume.AttachComponent< PostProcessComponent >();

			pp.tonemap.op    = eToneMappingOp::ACES;
			pp.tonemap.ev100 = 0.0f;
			pp.tonemap.gamma = 1.0f;
		};

	{
		MeshDescriptor descriptor = {};
		descriptor.rootPath          = ASSET_PATH;
		descriptor.bOptimize         = false; // keep authored vertices/indices verbatim
		descriptor.rendererAPI       = s_RendererAPI;
		descriptor.bWindingCW        = true;
		descriptor.bConvertToLeftHanded = false;
		descriptor.bGenerateMeshlets = false;
		descriptor.numLODs           = 1;

		const fs::path meshDir = ASSET_PATH / "Generated" / m_PathTracerReferenceScene / "meshes";

		const float3 white = float3(0.725f, 0.71f, 0.68f);
		const float3 red   = float3(0.63f, 0.065f, 0.05f);
		const float3 green = float3(0.14f, 0.45f, 0.091f);

		const auto forEachMaterialByMeshPath = [this](const fs::path& meshPath, auto&& fn)
		{
			const std::string meshPathString = meshPath.string();
			auto& registry = m_pScene->Registry();
			auto view = registry.view< StaticMeshComponent, MaterialComponent >();
			for (auto entity : view)
			{
				auto& mesh = view.get< StaticMeshComponent >(entity);
				if (mesh.path != meshPathString)
					continue;

				mesh.tag = meshPathString;
				fn(view.get< MaterialComponent >(entity));
				registry.patch< StaticMeshComponent >(entity, [](auto&) {});
				registry.patch< MaterialComponent >(entity, [](auto&) {});
			}
		};

		const auto forEachStaticMeshByMeshPath = [this](const fs::path& meshPath, auto&& fn)
		{
			const std::string meshPathString = meshPath.string();
			auto& registry = m_pScene->Registry();
			auto view = registry.view< StaticMeshComponent >();
			for (auto entity : view)
			{
				auto& mesh = view.get< StaticMeshComponent >(entity);
				if (mesh.path != meshPathString)
					continue;

				fn(mesh);
				registry.patch< StaticMeshComponent >(entity, [](auto&) {});
			}
		};

		if (IsGalleryScene(m_PathTracerReferenceScene))
		{
			const auto gallery = LoadGallerySceneManifest(m_PathTracerReferenceScene);
			if (gallery.bValid)
			{
				for (const auto& meshEntry : gallery.meshes)
				{
					const fs::path meshPath = meshEntry.path;
					auto entity = m_pScene->ImportModel(meshPath, descriptor);
					UNUSED(entity);
					forEachMaterialByMeshPath(meshPath, [&](MaterialComponent& mat)
					{
						mat.name = meshEntry.name;
						mat.tint = meshEntry.tint;
						mat.metallic = meshEntry.metallic;
						mat.roughness = meshEntry.roughness;
						mat.ior = meshEntry.ior;
						mat.transmission = meshEntry.transmission;
						mat.specularColor = meshEntry.specularColor;
						mat.specularStrength = meshEntry.specularStrength;
						mat.emissionColor = meshEntry.emissionColor;
						mat.emissivePower = meshEntry.emissivePower;
						mat.materialType = 0u;
						mat.clearcoat = 0.0f;
						mat.clearcoatRoughness = 0.0f;
						mat.sheenColor = float3(0.0f);
						mat.sheenRoughness = 0.0f;
						mat.subsurface = 0.0f;
						mat.bFaceNormals = meshEntry.bFaceNormals;
						mat.alphaCutoff = meshEntry.alphaCutoff;
						mat.albedoTex = meshEntry.albedoTex;
						mat.normalTex.clear();
						mat.roughnessTex.clear();
						mat.metallicTex.clear();
					});
				}

				for (const auto& area : gallery.areaLights)
				{
					auto areaLight = m_pScene->CreateEntity(area.name.empty() ? "Gallery Area Light" : area.name);
					areaLight.AttachComponent< LightComponent >();

					auto& light = areaLight.GetComponent< LightComponent >();
					light.type = eLightType::Area;
					light.color = area.color;
					light.temperatureK = 0.0f;
					light.luminousFluxLm = area.luminousFluxLm;

					auto& transformComponent = areaLight.GetComponent< TransformComponent >();
					transformComponent.transform.position = area.position;
					transformComponent.transform.rotation = area.rotation;
					transformComponent.transform.scale = area.scale;
				}
			}

			createPostProcessVolume();
			return;
		}
		if (IsEnvironmentMapTestScene(m_PathTracerReferenceScene))
		{
			const fs::path floorPath = meshDir / "floor.ply";
			auto floorEntity = m_pScene->ImportModel(floorPath, descriptor);
			UNUSED(floorEntity);
			forEachMaterialByMeshPath(floorPath, [](MaterialComponent& mat)
			{
				mat.tint      = float4(0.8f, 0.8f, 0.8f, 1.0f);
				mat.metallic  = 0.0f;
				mat.roughness = 1.0f;
			});

			const fs::path spherePath = ASSET_PATH / "Model" / "_synthetic" / "icosphere_unit_hi.ply";
			auto sphereEntity = m_pScene->ImportModel(spherePath, descriptor);
			auto& sphereTransform = sphereEntity.GetComponent< TransformComponent >();
			sphereTransform.transform.position = float3(0.0f, 0.0f, 0.7f);
			sphereTransform.transform.scale    = float3(0.7f);
			m_pScene->Registry().patch< TransformComponent >(sphereEntity.ID(), [](auto&) {});
			forEachMaterialByMeshPath(spherePath, [](MaterialComponent& mat)
			{
				mat.tint      = float4(0.75f, 0.55f, 0.35f, 1.0f);
				mat.metallic  = 0.0f;
				mat.roughness = 1.0f;
			});

			createPostProcessVolume();
			return;
		}
		const auto loadWall = [&](const char* file, const float3& albedo)
		{
			const fs::path meshPath = meshDir / file;
			auto entity = m_pScene->ImportModel(meshPath, descriptor);
			forEachMaterialByMeshPath(meshPath, [&](MaterialComponent& mat)
			{
				mat.tint      = float4(albedo, 1.0f);
				mat.metallic  = 0.0f;
				mat.roughness = 1.0f;
			});
			return entity;
		};

		if (IsComplexRoomTestScene(m_PathTracerReferenceScene))
		{
			const fs::path checkerPath = ASSET_PATH / "Generated" / m_PathTracerReferenceScene / "textures" / "checker.exr";
			const auto configureCheckerMaterial = [checkerPath](MaterialComponent& mat)
			{
				mat.tint      = float4(1.0f);
				mat.metallic  = 0.0f;
				mat.roughness = 1.0f;
				mat.albedoTex = checkerPath.string();
			};

			const auto flipTextureU = [&](const char* file)
			{
				forEachStaticMeshByMeshPath(meshDir / file, [](StaticMeshComponent& mesh)
				{
					for (u32 i = 0; i < mesh.numVertices; ++i)
						mesh.pVertices[i].uv.x = 1.0f - mesh.pVertices[i].uv.x;
				});
			};

			loadWall("floor.ply", float3(1.0f));
			flipTextureU("floor.ply");
			forEachMaterialByMeshPath(meshDir / "floor.ply", configureCheckerMaterial);

			loadWall("ceiling.ply", white);
			loadWall("left_wall.ply", (m_PathTracerReferenceScene == "cornell_sphere_light" || m_PathTracerReferenceScene == "cornell_directional_light" || m_PathTracerReferenceScene == "cornell_disk_light" || m_PathTracerReferenceScene == "cornell_spot_light" || m_PathTracerReferenceScene == "cornell_tube_light" || m_PathTracerReferenceScene == "cornell_many_lights") ? white : red);
			loadWall("right_wall.ply", (m_PathTracerReferenceScene == "cornell_sphere_light" || m_PathTracerReferenceScene == "cornell_directional_light" || m_PathTracerReferenceScene == "cornell_disk_light" || m_PathTracerReferenceScene == "cornell_spot_light" || m_PathTracerReferenceScene == "cornell_tube_light" || m_PathTracerReferenceScene == "cornell_many_lights") ? white : green);
			loadWall("back_wall.ply", float3(1.0f));
			flipTextureU("back_wall.ply");
			forEachMaterialByMeshPath(meshDir / "back_wall.ply", configureCheckerMaterial);

			const auto loadComplexObject = [&](const char* file, const float3& position, const float3& rotation, const float3& scale,
				auto&& configureMaterial)
			{
				const fs::path meshPath = meshDir / file;
				auto entity = m_pScene->ImportModel(meshPath, descriptor);
				auto& transform = entity.GetComponent< TransformComponent >();
				transform.transform.position = position;
				transform.transform.rotation = rotation;
				transform.transform.scale    = scale;
				m_pScene->Registry().patch< TransformComponent >(entity.ID(), [](auto&) {});
				forEachMaterialByMeshPath(meshPath, configureMaterial);
				return entity;
			};

			loadComplexObject("ball_warm.ply", float3(-0.60f, 0.26f, -0.30f), float3(0.0f), float3(0.26f),
				[](MaterialComponent& mat)
				{
					mat.tint      = float4(0.70f, 0.45f, 0.25f, 1.0f);
					mat.metallic  = 0.0f;
					mat.roughness = 1.0f;
				});
			loadComplexObject("ball_teal.ply", float3(0.62f, 0.26f, -0.45f), float3(0.0f), float3(0.26f),
				[](MaterialComponent& mat)
				{
					mat.tint      = float4(0.20f, 0.60f, 0.55f, 1.0f);
					mat.metallic  = 0.0f;
					mat.roughness = 1.0f;
				});
			loadComplexObject("ball_metal.ply", float3(0.50f, 0.30f, 0.05f), float3(0.0f), float3(0.30f),
				[](MaterialComponent& mat)
				{
					const float3 aluminumF0 = float3(0.92804748f, 0.91780447f, 0.91890865f);
					mat.tint             = float4(aluminumF0, 1.0f);
					mat.metallic         = 1.0f;
					mat.roughness        = 0.15f;
					mat.specularColor    = aluminumF0;
					mat.specularStrength = 1.0f;
				});
			loadComplexObject("ball_glass.ply", float3(-0.05f, 0.28f, -0.10f), float3(0.0f), float3(0.28f),
				[](MaterialComponent& mat)
				{
					mat.tint             = float4(1.0f);
					mat.metallic         = 0.0f;
					mat.roughness        = 0.15f;
					mat.ior              = 1.5f;
					mat.transmission     = 1.0f;
					mat.specularColor    = float3(1.0f);
					mat.specularStrength = 1.0f;
				});
			loadComplexObject("ellip_blue.ply", float3(-0.60f, 0.20f, 0.40f), float3(0.0f, 0.0f, 40.0f),
				float3(0.26f, 0.15f, 0.26f),
				[](MaterialComponent& mat)
				{
					mat.tint      = float4(0.35f, 0.45f, 0.80f, 1.0f);
					mat.metallic  = 0.0f;
					mat.roughness = 1.0f;
				});
			loadComplexObject("ellip_orange.ply", float3(0.35f, 0.22f, 0.55f), float3(55.0f, 0.0f, 0.0f),
				float3(0.26f, 0.15f, 0.26f),
				[](MaterialComponent& mat)
				{
					mat.tint      = float4(0.80f, 0.40f, 0.10f, 1.0f);
					mat.metallic  = 0.0f;
					mat.roughness = 1.0f;
				});
			loadComplexObject("ball_hi.ply", float3(0.08f, 0.17f, 0.70f), float3(0.0f), float3(0.17f),
				[](MaterialComponent& mat)
				{
					mat.tint      = float4(0.50f, 0.30f, 0.70f, 1.0f);
					mat.metallic  = 0.0f;
					mat.roughness = 1.0f;
				});

			auto lightEntity = loadWall("ceiling_light.ply", float3(0.0f));
			UNUSED(lightEntity);
			forEachMaterialByMeshPath(meshDir / "ceiling_light.ply", [](MaterialComponent& mat)
			{
				mat.emissionColor = float3(17.0f, 12.0f, 4.0f);
				mat.emissivePower = 1.0f;
			});

			{
				auto areaLight = m_pScene->CreateEntity("Cornell Area Light");
				areaLight.AttachComponent< LightComponent >();

				auto& light = areaLight.GetComponent< LightComponent >();
				light.type           = eLightType::Area;
				light.color          = float3(17.0f, 12.0f, 4.0f);
				light.temperatureK   = 0.0f;
				light.luminousFluxLm = 3.14159265358979323846f * 0.25f;

				auto& transformComponent = areaLight.GetComponent< TransformComponent >();
				transformComponent.transform.position = float3(0.0f, 1.99f, 0.0f);
				transformComponent.transform.rotation = float3(90.0f, 0.0f, 0.0f);
				transformComponent.transform.scale    = float3(0.5f, 0.5f, 1.0f);
			}

			createPostProcessVolume();
			return;
		}
		if (m_PathTracerReferenceScene == "dining_room")
		{
			loadWall("floor.ply", float3(1.0f));
			forEachStaticMeshByMeshPath(meshDir / "floor.ply", [](StaticMeshComponent& mesh)
			{
				for (u32 i = 0; i < mesh.numVertices; ++i)
					mesh.pVertices[i].uv.x = 1.0f - mesh.pVertices[i].uv.x;
			});
			forEachMaterialByMeshPath(meshDir / "floor.ply", [](MaterialComponent& mat)
			{
				mat.tint      = float4(1.0f);
				mat.metallic  = 0.0f;
				mat.roughness = 1.0f;
				mat.albedoTex = (ASSET_PATH / "Generated" / "dining_room" / "textures" / "checker.exr").string();
			});

			loadWall("ceiling.ply", white);
			loadWall("back_wall.ply", white);
			loadWall("left_wall.ply", (m_PathTracerReferenceScene == "cornell_sphere_light" || m_PathTracerReferenceScene == "cornell_directional_light" || m_PathTracerReferenceScene == "cornell_disk_light" || m_PathTracerReferenceScene == "cornell_spot_light" || m_PathTracerReferenceScene == "cornell_tube_light" || m_PathTracerReferenceScene == "cornell_many_lights") ? white : red);
			loadWall("right_wall.ply", (m_PathTracerReferenceScene == "cornell_sphere_light" || m_PathTracerReferenceScene == "cornell_directional_light" || m_PathTracerReferenceScene == "cornell_disk_light" || m_PathTracerReferenceScene == "cornell_spot_light" || m_PathTracerReferenceScene == "cornell_tube_light" || m_PathTracerReferenceScene == "cornell_many_lights") ? white : green);

			const auto loadDiningObject = [&](const char* file, const float3& position, const float3& rotation, const float3& scale,
				auto&& configureMaterial)
			{
				const fs::path meshPath = meshDir / file;
				auto entity = m_pScene->ImportModel(meshPath, descriptor);
				auto& transform = entity.GetComponent< TransformComponent >();
				transform.transform.position = position;
				transform.transform.rotation = rotation;
				transform.transform.scale    = scale;
				m_pScene->Registry().patch< TransformComponent >(entity.ID(), [](auto&) {});
				forEachMaterialByMeshPath(meshPath, configureMaterial);
				return entity;
			};

			loadDiningObject("ball_warm.ply", float3(-0.5f, 0.32f, -0.15f), float3(0.0f), float3(0.32f),
				[](MaterialComponent& mat)
				{
					mat.tint      = float4(0.70f, 0.45f, 0.25f, 1.0f);
					mat.metallic  = 0.0f;
					mat.roughness = 1.0f;
				});

			loadDiningObject("ball_metal.ply", float3(0.5f, 0.32f, -0.30f), float3(0.0f), float3(0.32f),
				[](MaterialComponent& mat)
				{
					const float3 aluminumF0 = float3(0.92804748f, 0.91780447f, 0.91890865f);
					mat.tint             = float4(aluminumF0, 1.0f);
					mat.metallic         = 1.0f;
					mat.roughness        = 0.15f;
					mat.specularColor    = aluminumF0;
					mat.specularStrength = 1.0f;
				});

			loadDiningObject("ball_glass.ply", float3(0.08f, 0.30f, 0.42f), float3(0.0f), float3(0.30f),
				[](MaterialComponent& mat)
				{
					mat.tint             = float4(1.0f);
					mat.metallic         = 0.0f;
					mat.roughness        = 0.15f;
					mat.ior              = 1.5f;
					mat.transmission     = 1.0f;
					mat.specularColor    = float3(1.0f);
					mat.specularStrength = 1.0f;
				});

			loadDiningObject("ellipsoid.ply", float3(-0.45f, 0.28f, 0.45f), float3(0.0f, 0.0f, 40.0f),
				float3(0.30f, 0.18f, 0.30f),
				[](MaterialComponent& mat)
				{
					mat.tint      = float4(0.35f, 0.45f, 0.80f, 1.0f);
					mat.metallic  = 0.0f;
					mat.roughness = 1.0f;
				});

			auto lightEntity = loadWall("ceiling_light.ply", float3(0.0f));
			UNUSED(lightEntity);
			forEachMaterialByMeshPath(meshDir / "ceiling_light.ply", [](MaterialComponent& mat)
			{
				mat.emissionColor = float3(17.0f, 12.0f, 4.0f);
				mat.emissivePower = 1.0f;
			});

			{
				auto areaLight = m_pScene->CreateEntity("Cornell Area Light");
				areaLight.AttachComponent< LightComponent >();

				auto& light = areaLight.GetComponent< LightComponent >();
				light.type           = eLightType::Area;
				light.color          = float3(17.0f, 12.0f, 4.0f);
				light.temperatureK   = 0.0f;
				light.luminousFluxLm = 3.14159265358979323846f * 0.25f;

				auto& transformComponent = areaLight.GetComponent< TransformComponent >();
				transformComponent.transform.position = float3(0.0f, 1.99f, 0.0f);
				transformComponent.transform.rotation = float3(90.0f, 0.0f, 0.0f);
				transformComponent.transform.scale    = float3(0.5f, 0.5f, 1.0f);
			}

			createPostProcessVolume();
			return;
		}

		const bool bMaterialMapScene = m_PathTracerReferenceScene == "cornell_material_maps";
		const bool bNormalMapScene = m_PathTracerReferenceScene == "cornell_normal_map";
		const bool bAnisotropicConductorScene = m_PathTracerReferenceScene == "cornell_anisotropic_conductor";
		const bool bTexturedFloorScene = m_PathTracerReferenceScene == "cornell_textured" || bMaterialMapScene || bNormalMapScene;
		loadWall("floor.ply", bTexturedFloorScene ? float3(1.0f) : white);
		if (m_PathTracerReferenceScene == "cornell_textured")
		{
			const fs::path checkerPath = ASSET_PATH / "Generated" / m_PathTracerReferenceScene / "textures" / "checker.exr";
			forEachStaticMeshByMeshPath(meshDir / "floor.ply", [](StaticMeshComponent& mesh)
			{
				for (u32 i = 0; i < mesh.numVertices; ++i)
					mesh.pVertices[i].uv.x = 1.0f - mesh.pVertices[i].uv.x;
			});
			forEachMaterialByMeshPath(meshDir / "floor.ply", [checkerPath](MaterialComponent& mat)
			{
				mat.tint      = float4(1.0f);
				mat.albedoTex = checkerPath.string();
			});
		}
		else if (bMaterialMapScene)
		{
			const fs::path roughnessPath = ASSET_PATH / "Generated" / m_PathTracerReferenceScene / "textures" / "roughness_map.exr";
			const float3 aluminumF0 = float3(0.92804748f, 0.91780447f, 0.91890865f);
			forEachStaticMeshByMeshPath(meshDir / "floor.ply", [](StaticMeshComponent& mesh)
			{
				for (u32 i = 0; i < mesh.numVertices; ++i)
					mesh.pVertices[i].uv.y = 1.0f - mesh.pVertices[i].uv.y;
			});
			forEachMaterialByMeshPath(meshDir / "floor.ply", [roughnessPath, aluminumF0](MaterialComponent& mat)
			{
				mat.tint             = float4(aluminumF0, 1.0f);
				mat.metallic         = 1.0f;
				mat.roughness        = 1.0f;
				mat.roughnessTex     = roughnessPath.string();
				mat.specularColor    = aluminumF0;
				mat.specularStrength = 1.0f;
				mat.transmission     = 0.0f;
			});
		}
		else if (bNormalMapScene)
		{
			const fs::path normalPath = ASSET_PATH / "Generated" / m_PathTracerReferenceScene / "textures" / "normal_map.exr";
			forEachStaticMeshByMeshPath(meshDir / "floor.ply", [](StaticMeshComponent& mesh)
			{
				for (u32 i = 0; i < mesh.numVertices; ++i)
					mesh.pVertices[i].uv.y = 1.0f - mesh.pVertices[i].uv.y;
			});
			forEachMaterialByMeshPath(meshDir / "floor.ply", [normalPath](MaterialComponent& mat)
			{
				mat.tint      = float4(0.72f, 0.70f, 0.66f, 1.0f);
				mat.metallic  = 0.0f;
				mat.roughness = 1.0f;
				mat.normalTex = normalPath.string();
			});
		}
		loadWall("ceiling.ply",   white);
		if (m_PathTracerReferenceScene != "cornell_open")
			loadWall("back_wall.ply", white);
		loadWall("left_wall.ply", (m_PathTracerReferenceScene == "cornell_sphere_light" || m_PathTracerReferenceScene == "cornell_directional_light" || m_PathTracerReferenceScene == "cornell_disk_light" || m_PathTracerReferenceScene == "cornell_spot_light" || m_PathTracerReferenceScene == "cornell_tube_light" || m_PathTracerReferenceScene == "cornell_many_lights") ? white : red);
		loadWall("right_wall.ply", (m_PathTracerReferenceScene == "cornell_sphere_light" || m_PathTracerReferenceScene == "cornell_directional_light" || m_PathTracerReferenceScene == "cornell_disk_light" || m_PathTracerReferenceScene == "cornell_spot_light" || m_PathTracerReferenceScene == "cornell_tube_light" || m_PathTracerReferenceScene == "cornell_many_lights") ? white : green);
		if (bAnisotropicConductorScene)
		{
			const fs::path panelPath = meshDir / "anisotropic_panel.ply";
			auto panelEntity = m_pScene->ImportModel(panelPath, descriptor);
			UNUSED(panelEntity);

			const float3 aluminumF0 = float3(0.92804748f, 0.91780447f, 0.91890865f);
			forEachMaterialByMeshPath(panelPath, [aluminumF0](MaterialComponent& mat)
			{
				mat.tint               = float4(aluminumF0, 1.0f);
				mat.metallic           = 1.0f;
				mat.roughness          = 0.22f;
				mat.anisotropy         = 0.8f;
				mat.anisotropyRotation = 0.0f;
				mat.specularColor      = aluminumF0;
				mat.specularStrength   = 1.0f;
				mat.transmission       = 0.0f;
			});
		}
		if (m_PathTracerReferenceScene == "cornell_mesh")
			loadWall("blob.ply", float3(0.35f, 0.45f, 0.8f));
		else if (m_PathTracerReferenceScene == "cornell_transform" || m_PathTracerReferenceScene == "cornell_transform_rot" ||
			m_PathTracerReferenceScene == "cornell_transform_model")
		{
			const bool bComplexTransformModel = m_PathTracerReferenceScene == "cornell_transform_model";
			auto meshEntity = loadWall(bComplexTransformModel ? "model.ply" : "blob.ply",
				bComplexTransformModel ? float3(0.42f, 0.58f, 0.78f) : float3(0.35f, 0.45f, 0.8f));
			auto& meshTransform = meshEntity.GetComponent< TransformComponent >();
			if (m_PathTracerReferenceScene == "cornell_transform_rot")
			{
				meshTransform.transform.position = float3(0.0f, 0.55f, 0.0f);
				meshTransform.transform.rotation = float3(0.0f, 0.0f, 35.0f);
				meshTransform.transform.scale    = float3(0.5f, 0.28f, 0.5f);
			}
			else if (bComplexTransformModel)
			{
				meshTransform.transform.position = float3(0.02f, 0.54f, 0.02f);
				meshTransform.transform.rotation = float3(0.0f, 0.0f, 35.0f);
				meshTransform.transform.scale    = float3(0.34f, 0.31f, 0.42f);
			}
			else
			{
				meshTransform.transform.position = float3(0.0f, 0.4f, 0.0f);
				meshTransform.transform.scale    = float3(0.4f);
			}
			m_pScene->Registry().patch< TransformComponent >(meshEntity.ID(), [](auto&) {});
		}
		else if (m_PathTracerReferenceScene == "cornell_box_conductor" || m_PathTracerReferenceScene == "cornell_box_conductor_smooth" ||
			m_PathTracerReferenceScene == "cornell_box_mixed_metallic" || m_PathTracerReferenceScene == "cornell_box_opaque_dielectric" || m_PathTracerReferenceScene == "cornell_box_mixed_transmission" || m_PathTracerReferenceScene == "cornell_directional_light" || m_PathTracerReferenceScene == "cornell_disk_light" || m_PathTracerReferenceScene == "cornell_spot_light" || m_PathTracerReferenceScene == "cornell_tube_light" || m_PathTracerReferenceScene == "cornell_many_lights" ||
			m_PathTracerReferenceScene == "cornell_box_dielectric" || m_PathTracerReferenceScene == "cornell_box_dielectric_smooth" ||
			m_PathTracerReferenceScene == "cornell_principled_glass" || IsPrincipledMaterialTestScene(m_PathTracerReferenceScene))
		{
			const fs::path spherePath = ASSET_PATH / "Model" / "_synthetic" / "icosphere_unit_hi.ply";
			auto sphereEntity = m_pScene->ImportModel(spherePath, descriptor);
			auto& sphereTransform = sphereEntity.GetComponent< TransformComponent >();
			sphereTransform.transform.position = float3(0.0f, 0.4f, 0.0f);
			sphereTransform.transform.scale    = float3(0.4f);
			m_pScene->Registry().patch< TransformComponent >(sphereEntity.ID(), [](auto&) {});

			if (m_PathTracerReferenceScene == "cornell_directional_light" || m_PathTracerReferenceScene == "cornell_disk_light" || m_PathTracerReferenceScene == "cornell_spot_light" || m_PathTracerReferenceScene == "cornell_tube_light" || m_PathTracerReferenceScene == "cornell_many_lights")
			{
				forEachMaterialByMeshPath(spherePath, [](MaterialComponent& mat)
				{
					mat.tint             = float4(0.55f, 0.55f, 0.55f, 1.0f);
					mat.metallic         = 0.0f;
					mat.roughness        = 1.0f;
					mat.transmission     = 0.0f;
					mat.specularStrength = 0.0f;
				});
			}
			else if (m_PathTracerReferenceScene == "cornell_box_conductor" || m_PathTracerReferenceScene == "cornell_box_conductor_smooth")
			{
				const float3 aluminumF0 = float3(0.92804748f, 0.91780447f, 0.91890865f);
				const f32 roughness = m_PathTracerReferenceScene == "cornell_box_conductor_smooth" ? 0.0f : 0.15f;
				forEachMaterialByMeshPath(spherePath, [aluminumF0, roughness](MaterialComponent& mat)
				{
					mat.tint             = float4(aluminumF0, 1.0f);
					mat.metallic         = 1.0f;
					mat.roughness        = roughness;
					mat.specularColor    = aluminumF0;
					mat.specularStrength = 1.0f;
				});
			}
			else if (m_PathTracerReferenceScene == "cornell_box_mixed_metallic")
			{
				const float3 baseColor  = float3(0.55f, 0.12f, 0.14f);
				const float3 aluminumF0 = float3(0.92804748f, 0.91780447f, 0.91890865f);
				forEachMaterialByMeshPath(spherePath, [baseColor, aluminumF0](MaterialComponent& mat)
				{
					mat.tint             = float4(baseColor, 1.0f);
					mat.metallic         = 0.5f;
					mat.roughness        = 0.35f;
					mat.transmission     = 0.0f;
					mat.specularColor    = aluminumF0;
					mat.specularStrength = 1.0f;
				});
			}
			else if (m_PathTracerReferenceScene == "cornell_box_opaque_dielectric")
			{
				const float3 baseColor = float3(0.55f, 0.12f, 0.14f);
				forEachMaterialByMeshPath(spherePath, [baseColor](MaterialComponent& mat)
				{
					mat.tint             = float4(baseColor, 1.0f);
					mat.metallic         = 0.0f;
					mat.roughness        = 0.25f;
					mat.ior              = 1.5f;
					mat.transmission     = 0.0f;
					mat.specularColor    = float3(1.0f);
					mat.specularStrength = 1.0f;
				});
			}
			else if (m_PathTracerReferenceScene == "cornell_box_mixed_transmission")
			{
				const float3 baseColor = float3(0.55f, 0.12f, 0.14f);
				forEachMaterialByMeshPath(spherePath, [baseColor](MaterialComponent& mat)
				{
					mat.tint             = float4(baseColor, 1.0f);
					mat.metallic         = 0.0f;
					mat.roughness        = 0.15f;
					mat.ior              = 1.5f;
					mat.transmission     = 0.5f;
					mat.specularColor    = float3(1.0f);
					mat.specularStrength = 1.0f;
				});
			}
			else if (IsPrincipledMaterialTestScene(m_PathTracerReferenceScene))
			{
				float3 baseColor = float3(0.55f, 0.12f, 0.14f);
				f32 roughness = 0.35f;
				f32 eta = 1.5f;
				f32 specTint = 0.3f;
				f32 sheen = 0.6f;
				f32 sheenTint = 0.5f;
				f32 clearcoat = 1.0f;
				f32 clearcoatGloss = 0.85f;

				if (m_PathTracerReferenceScene == "cornell_principled_diffuse")
				{
					eta = 1.0f;
					specTint = 0.0f;
					sheen = 0.0f;
					clearcoat = 0.0f;
					clearcoatGloss = 0.0f;
				}
				else if (m_PathTracerReferenceScene == "cornell_principled_specular")
				{
					baseColor = float3(0.0f);
					specTint = 0.0f;
					sheen = 0.0f;
					clearcoat = 0.0f;
					clearcoatGloss = 0.0f;
				}
				else if (m_PathTracerReferenceScene == "cornell_principled_clearcoat")
				{
					baseColor = float3(0.0f);
					eta = 1.0f;
					specTint = 0.0f;
					sheen = 0.0f;
					clearcoat = 1.0f;
					clearcoatGloss = 0.85f;
				}
				else if (m_PathTracerReferenceScene == "cornell_principled_sheen")
				{
					baseColor = float3(0.0f);
					eta = 1.0f;
					specTint = 0.0f;
					sheen = 1.0f;
					sheenTint = 0.0f;
					clearcoat = 0.0f;
					clearcoatGloss = 0.0f;
				}

				const f32 baseLuminance = baseColor.x * 0.2126f + baseColor.y * 0.7152f + baseColor.z * 0.0722f;
				const float3 colorTint = baseLuminance > 1.0e-4f ? baseColor / baseLuminance : float3(1.0f);
				const f32 f0 = ((eta - 1.0f) / (eta + 1.0f)) * ((eta - 1.0f) / (eta + 1.0f));
				const float3 specularTint = float3(1.0f) * (1.0f - specTint) + colorTint * specTint;
				const float3 sheenColor = sheen * (float3(1.0f) * (1.0f - sheenTint) + colorTint * sheenTint);
				const f32 clearcoatAlpha = clearcoat > 0.0f ? 0.1f * (1.0f - clearcoatGloss) + 0.001f * clearcoatGloss : 0.0f;

				forEachMaterialByMeshPath(spherePath, [baseColor, roughness, eta, f0, specularTint, sheenColor, clearcoat, clearcoatAlpha](MaterialComponent& mat)
				{
					mat.tint               = float4(baseColor, 1.0f);
					mat.metallic           = 0.0f;
					mat.roughness          = roughness;
					mat.ior                = eta;
					mat.transmission       = 0.0f;
					mat.specularColor      = f0 * specularTint;
					mat.specularStrength   = 1.0f;
					mat.clearcoat          = clearcoat;
					mat.clearcoatRoughness = clearcoatAlpha;
					mat.sheenColor         = sheenColor;
					mat.sheenRoughness     = roughness;
					mat.materialType       = kPathTracerPrincipledMaterialType;
				});
			}
			else
			{
				forEachMaterialByMeshPath(spherePath, [this](MaterialComponent& mat)
				{
					mat.tint             = float4(1.0f);
					mat.metallic         = 0.0f;
					mat.roughness        = m_PathTracerReferenceScene == "cornell_box_dielectric_smooth" ? 0.0f :
						(m_PathTracerReferenceScene == "cornell_principled_glass" ? 0.16f : 0.15f);
					mat.ior              = 1.5f;
					mat.transmission     = 1.0f;
					mat.specularColor    = float3(1.0f);
					mat.specularStrength = 1.0f;
				});
			}
		}

		if (m_PathTracerReferenceScene == "cornell_many_lights")
		{
			constexpr f32 kPi = 3.14159265358979323846f;
			constexpr f32 kDegToRad = kPi / 180.0f;

			{
				auto directionalLight = m_pScene->CreateEntity("Cornell Many Directional Light");
				directionalLight.AttachComponent< LightComponent >();

				auto& light = directionalLight.GetComponent< LightComponent >();
				light.type             = eLightType::Directional;
				light.color            = float3(1.0f);
				light.temperatureK     = 0.0f;
				light.illuminanceLux   = 0.9f;
				light.angularRadiusRad = 0.0f;

				auto& transformComponent = directionalLight.GetComponent< TransformComponent >();
				transformComponent.transform.position = float3(0.0f, 0.18f, 1.0f);
			}

			{
				auto spotLight = m_pScene->CreateEntity("Cornell Many Spot Light");
				spotLight.AttachComponent< LightComponent >();

				auto& light = spotLight.GetComponent< LightComponent >();
				light.type              = eLightType::Spot;
				light.color             = float3(1.0f, 0.94f, 0.82f);
				light.temperatureK      = 0.0f;
				light.radiusM           = 0.0f;
				light.luminousFluxLm    = 1.2f;
				light.innerConeAngleRad = 16.0f * kDegToRad;
				light.outerConeAngleRad = 28.0f * kDegToRad;

				auto& transformComponent = spotLight.GetComponent< TransformComponent >();
				transformComponent.transform.position = float3(0.0f, 1.85f, 0.65f);
				transformComponent.transform.rotation = float3(90.0f, 0.0f, 0.0f);
			}

			{
				constexpr f32 diskRadius = 0.28f;
				const float3 diskRadiance = float3(9.0f, 7.0f, 3.0f);
				auto diskLight = m_pScene->CreateEntity("Cornell Many Disk Light");
				diskLight.AttachComponent< LightComponent >();

				auto& light = diskLight.GetComponent< LightComponent >();
				light.type           = eLightType::Disk;
				light.color          = diskRadiance;
				light.temperatureK   = 0.0f;
				light.diskRadiusM    = diskRadius;
				light.luminousFluxLm = kPi * kPi * diskRadius * diskRadius;

				auto& transformComponent = diskLight.GetComponent< TransformComponent >();
				transformComponent.transform.position = float3(-0.45f, 2.35f, 1.35f);
				transformComponent.transform.rotation = float3(90.0f, 0.0f, 0.0f);
			}

			{
				constexpr f32 sphereRadius = 0.22f;
				const float3 sphereRadiance = float3(6.0f, 8.0f, 14.0f);
				auto sphereLight = m_pScene->CreateEntity("Cornell Many Sphere Light");
				sphereLight.AttachComponent< LightComponent >();

				auto& light = sphereLight.GetComponent< LightComponent >();
				light.type           = eLightType::Sphere;
				light.color          = sphereRadiance;
				light.temperatureK   = 0.0f;
				light.radiusM        = sphereRadius;
				light.luminousFluxLm = kPi * 4.0f * kPi * sphereRadius * sphereRadius;

				auto& transformComponent = sphereLight.GetComponent< TransformComponent >();
				transformComponent.transform.position = float3(0.45f, 2.35f, 1.35f);
			}

			{
				constexpr f32 tubeRadius = 0.07f;
				constexpr f32 tubeLength = 0.80f;
				const float3 tubeRadiance = float3(5.0f, 10.0f, 8.0f);
				auto tubeLight = m_pScene->CreateEntity("Cornell Many Tube Light");
				tubeLight.AttachComponent< LightComponent >();

				auto& light = tubeLight.GetComponent< LightComponent >();
				light.type           = eLightType::Tube;
				light.color          = tubeRadiance;
				light.temperatureK   = 0.0f;
				light.tubeRadiusM    = tubeRadius;
				light.tubeLengthM    = tubeLength;
				light.luminousFluxLm = 2.0f * kPi * kPi * tubeRadius * tubeLength;

				auto& transformComponent = tubeLight.GetComponent< TransformComponent >();
				transformComponent.transform.position = float3(0.0f, 2.42f, 1.45f);
				transformComponent.transform.rotation = float3(0.0f, 90.0f, 0.0f);
			}
		}
		else if (m_PathTracerReferenceScene == "cornell_directional_light")
		{
			auto directionalLight = m_pScene->CreateEntity("Cornell Directional Light");
			directionalLight.AttachComponent< LightComponent >();

			auto& light = directionalLight.GetComponent< LightComponent >();
			light.type             = eLightType::Directional;
			light.color            = float3(1.0f);
			light.temperatureK     = 0.0f;
			light.illuminanceLux   = 3.0f;
			light.angularRadiusRad = 0.0f;

			auto& transformComponent = directionalLight.GetComponent< TransformComponent >();
			transformComponent.transform.position = float3(0.0f, 0.18f, 1.0f);
		}
		else if (m_PathTracerReferenceScene == "cornell_spot_light")
		{
			constexpr f32 kDegToRad = 3.14159265358979323846f / 180.0f;
			auto spotLight = m_pScene->CreateEntity("Cornell Spot Light");
			spotLight.AttachComponent< LightComponent >();

			auto& light = spotLight.GetComponent< LightComponent >();
			light.type              = eLightType::Spot;
			light.color             = float3(1.0f);
			light.temperatureK      = 0.0f;
			light.radiusM           = 0.0f;
			light.luminousFluxLm    = 3.0f;
			light.innerConeAngleRad = 16.0f * kDegToRad;
			light.outerConeAngleRad = 28.0f * kDegToRad;

			auto& transformComponent = spotLight.GetComponent< TransformComponent >();
			transformComponent.transform.position = float3(0.0f, 1.85f, 0.65f);
			transformComponent.transform.rotation = float3(90.0f, 0.0f, 0.0f);
		}
		else if (m_PathTracerReferenceScene == "cornell_disk_light")
		{
			constexpr f32 diskRadius = 0.22f;
			const float3 diskRadiance = float3(30.0f, 22.0f, 8.0f);
			auto diskLight = m_pScene->CreateEntity("Cornell Disk Light");
			diskLight.AttachComponent< LightComponent >();

			auto& light = diskLight.GetComponent< LightComponent >();
			light.type           = eLightType::Disk;
			light.color          = diskRadiance;
			light.temperatureK   = 0.0f;
			light.diskRadiusM    = diskRadius;
			light.luminousFluxLm = 3.14159265358979323846f * 3.14159265358979323846f * diskRadius * diskRadius;

			auto& transformComponent = diskLight.GetComponent< TransformComponent >();
			transformComponent.transform.position = float3(0.0f, 2.35f, 1.35f);
			transformComponent.transform.rotation = float3(90.0f, 0.0f, 0.0f);
		}
		else if (m_PathTracerReferenceScene == "cornell_tube_light")
		{
			constexpr f32 tubeRadius = 0.05f;
			constexpr f32 tubeLength = 0.70f;
			const float3 tubeRadiance = float3(30.0f, 22.0f, 8.0f);
			auto tubeLight = m_pScene->CreateEntity("Cornell Tube Light");
			tubeLight.AttachComponent< LightComponent >();

			auto& light = tubeLight.GetComponent< LightComponent >();
			light.type           = eLightType::Tube;
			light.color          = tubeRadiance;
			light.temperatureK   = 0.0f;
			light.tubeRadiusM    = tubeRadius;
			light.tubeLengthM    = tubeLength;
			light.luminousFluxLm = 2.0f * 3.14159265358979323846f * 3.14159265358979323846f * tubeRadius * tubeLength;

			auto& transformComponent = tubeLight.GetComponent< TransformComponent >();
			transformComponent.transform.position = float3(0.0f, 2.35f, 1.35f);
			transformComponent.transform.rotation = float3(0.0f, 90.0f, 0.0f);
		}
		else if (m_PathTracerReferenceScene == "cornell_sphere_light")
		{
			constexpr f32 sphereRadius = 0.18f;
			const float3 sphereRadiance = float3(30.0f, 22.0f, 8.0f);
			auto sphereLight = m_pScene->CreateEntity("Cornell Sphere Light");
			sphereLight.AttachComponent< LightComponent >();

			auto& light = sphereLight.GetComponent< LightComponent >();
			light.type           = eLightType::Sphere;
			light.color          = sphereRadiance;
			light.temperatureK   = 0.0f;
			light.radiusM        = sphereRadius;
			light.luminousFluxLm = 3.14159265358979323846f * 4.0f * 3.14159265358979323846f * sphereRadius * sphereRadius;

			auto& transformComponent = sphereLight.GetComponent< TransformComponent >();
			transformComponent.transform.position = float3(0.0f, 2.35f, 1.35f);
		}
		else if (m_PathTracerReferenceScene != "cornell_open")
		{
			auto lightEntity = loadWall("ceiling_light.ply", float3(0.0f));
			UNUSED(lightEntity);
			forEachMaterialByMeshPath(meshDir / "ceiling_light.ply", [](MaterialComponent& mat)
			{
				mat.emissionColor = float3(17.0f, 12.0f, 4.0f);
				mat.emissivePower = 1.0f;
			});

			{
				auto areaLight = m_pScene->CreateEntity("Cornell Area Light");
				areaLight.AttachComponent< LightComponent >();

				auto& light = areaLight.GetComponent< LightComponent >();
				light.type           = eLightType::Area;
				light.color          = float3(17.0f, 12.0f, 4.0f);
				light.temperatureK   = 0.0f;
				light.luminousFluxLm = 3.14159265358979323846f * 0.25f;

				auto& transformComponent = areaLight.GetComponent< TransformComponent >();
				transformComponent.transform.position = float3(0.0f, 1.99f, 0.0f);
				transformComponent.transform.rotation = float3(90.0f, 0.0f, 0.0f);
				transformComponent.transform.scale    = float3(0.5f, 0.5f, 1.0f);
			}
		}
	}

	// post-process volume
	{
		createPostProcessVolume();
	}
}
