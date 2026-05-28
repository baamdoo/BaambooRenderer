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

using namespace baamboo;

void RayTracingApp::Initialize(eRendererAPI api)
{
	m_DeviceSettings.bRaytracing = true;
	m_DeviceSettings.bMeshShader = true;

	Super::Initialize(api);

	/*if (m_MitsubaSensor.bValid)
		m_CameraController.SetLookAt(m_MitsubaSensor.position, m_MitsubaSensor.target);
	else
		m_CameraController.SetLookAt(float3(0.0, 0.0f, -5.0f), float3(0.0f, 0.0f, 1.0f));

	m_pCamera = new EditorCamera(m_CameraController, m_pWindow->Width(), m_pWindow->Height());
	if (m_MitsubaSensor.bValid)
	{
		m_pCamera->fov   = m_MitsubaSensor.fovYDeg;
		m_pCamera->zNear = m_MitsubaSensor.nearClip;
		m_pCamera->zFar  = m_MitsubaSensor.farClip;
	}*/
}

void RayTracingApp::Update(float dt)
{
	Super::Update(dt);

	m_CameraController.Update(dt);
}

void RayTracingApp::Release()
{
	m_CameraController.Reset();
	m_pPathTracerNode = Weak< PathTracerNode >();

	Super::Release();
}

bool RayTracingApp::InitWindow()
{
	// **
	// Create window
	// **
	WindowDescriptor windowDesc = { .numDesiredImages = 3, .bVSync = false };
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
	m_pPathTracerNode = pPathTracerNode;
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

	/*const fs::path mitsubaScenePath = MODEL_PATH / "staircase2" / "scene_v3.xml";
	if (fs::exists(mitsubaScenePath))
	{
		MitsubaLoadSettings settings = {};
		settings.meshDescriptor.rootPath          = GetModelPath();
		settings.meshDescriptor.bOptimize         = true;
		settings.meshDescriptor.rendererAPI       = s_RendererAPI;
		settings.meshDescriptor.bWindingCW        = true;
		settings.meshDescriptor.bGenerateMeshlets = false;
		settings.meshDescriptor.numLODs           = 1;

		MitsubaLoadResult result = MitsubaLoader::Load(*m_pScene, mitsubaScenePath, settings);
		if (result.bSuccess)
		{
			m_MitsubaSensor = result.sensor;
			Arc< PathTracerNode > pPathTracerNode = m_pPathTracerNode.lock();
			if (pPathTracerNode && result.integrator.bValid && result.integrator.maxDepth > 0)
			{
				pPathTracerNode->settings.maxDepth = result.integrator.maxDepth;
				pPathTracerNode->settings.bRequestReset = true;
			}

			for (const std::string& warning : result.warnings)
				printf("[MitsubaLoader] %s\n", warning.c_str());

			createPostProcessVolume();
			return;
		}

		for (const std::string& warning : result.warnings)
			printf("[MitsubaLoader] %s\n", warning.c_str());
	}*/
	/*MeshDescriptor descriptor = {};
	descriptor.rootPath = GetModelPath();
	descriptor.bOptimize = true;
	descriptor.rendererAPI = s_RendererAPI;
	descriptor.bWindingCW = true;
	descriptor.bGenerateMeshlets = false;

	auto entity = m_pScene->ImportModel(MODEL_PATH.append("GlamVelvetSofa.glb"), descriptor);
	entity.AttachComponent< ScriptComponent >();

	auto sunLight = m_pScene->CreateEntity("Skybox");
	auto& atmosphere = sunLight.AttachComponent< AtmosphereComponent >();
	atmosphere.skybox = TEXTURE_PATH.string() + "studio_small.exr";

	auto light = m_pScene->CreateEntity("Punctual Light");
	light.AttachComponent< LightComponent >();

	auto& lc = light.GetComponent< LightComponent >();
	lc.type           = eLightType::Sphere;
	lc.temperatureK   = 10000.0f;
	lc.color          = float3(1.0f, 0.95f, 0.8f);
	lc.radiusM        = 0.5f;
	lc.luminousFluxLm = 800.0f;

	auto& lightXform = light.GetComponent< TransformComponent >().transform;
	lightXform.position = float3(0.0f, 3.0f, 0.0f);*/

	// static mesh
	{
		MeshDescriptor descriptor = {};
		descriptor.rootPath = GetModelPath();
		descriptor.bOptimize = true;
		descriptor.rendererAPI = s_RendererAPI;
		descriptor.bWindingCW = true;
		descriptor.bGenerateMeshlets = false;

		auto entity = m_pScene->ImportModel(MODEL_PATH.append("DamagedHelmet/DamagedHelmet.gltf"), descriptor);
		entity.AttachComponent< ScriptComponent >();

		const float3 scaleFloor = float3(10.0f, 0.4f, 10.0f);
		const float3 scaleSide = float3(0.4f, 10.0f, 10.0f);
		const float3 scaleEnd = float3(10.0f, 10.0f, 0.4f);

		auto floor = m_pScene->ImportModel(MODEL_PATH.append("cube.obj"), descriptor);
		floor.GetComponent< TransformComponent >().transform.position = float3(0.0f, -5.0f, 0.0f);
		floor.GetComponent< TransformComponent >().transform.scale = scaleFloor;

		auto ceiling = m_pScene->ImportModel(MODEL_PATH.append("cube.obj"), descriptor);
		ceiling.GetComponent< TransformComponent >().transform.position = float3(0.0f, 5.0f, 0.0f);
		ceiling.GetComponent< TransformComponent >().transform.scale = scaleFloor;

		auto leftWall = m_pScene->ImportModel(MODEL_PATH.append("cube.obj"), descriptor);
		leftWall.GetComponent< TransformComponent >().transform.position = float3(-10.0f, 0.0f, 0.0f);
		leftWall.GetComponent< TransformComponent >().transform.scale = scaleSide;

		auto rightWall = m_pScene->ImportModel(MODEL_PATH.append("cube.obj"), descriptor);
		rightWall.GetComponent< TransformComponent >().transform.position = float3(10.0f, 0.0f, 0.0f);
		rightWall.GetComponent< TransformComponent >().transform.scale = scaleSide;

		auto backWall = m_pScene->ImportModel(MODEL_PATH.append("cube.obj"), descriptor);
		backWall.GetComponent< TransformComponent >().transform.position = float3(0.0f, 0.0f, 10.0f);
		backWall.GetComponent< TransformComponent >().transform.scale = scaleEnd;

		auto frontWall = m_pScene->ImportModel(MODEL_PATH.append("cube.obj"), descriptor);
		frontWall.GetComponent< TransformComponent >().transform.position = float3(0.0f, 0.0f, -10.0f);
		frontWall.GetComponent< TransformComponent >().transform.scale = scaleEnd;

		auto areaLight = m_pScene->CreateEntity("AreaLight");
		auto& areaXform = areaLight.GetComponent< TransformComponent >().transform;
		areaXform.position = float3(0.0f, 4.5f, 0.0f);
		areaXform.rotation = float3(90.0f, 0.0f, 0.0f);
		auto& areaLC = areaLight.AttachComponent< LightComponent >();
		areaLC.SetDefaultArea();
		areaLC.color = float3(1.0f, 1.0f, 1.0f);
		areaLC.luminousFluxLm = 50.0f;
	}
	// post-process volume
	{
		createPostProcessVolume();
	}
}
