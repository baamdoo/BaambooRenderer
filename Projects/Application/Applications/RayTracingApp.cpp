#include "RayTracingApp.h"

#include "BaambooCore/Common.h"
#include "BaambooCore/Window.h"
#include "BaambooCore/Input.hpp"

#include "BaambooScene/Entity.h"
#include "BaambooScene/Components.h"
#include "BaambooScene/RenderNodes/SkyboxNode.h"
#include "BaambooScene/RenderNodes/RaytracingNode.h"
#include "BaambooScene/RenderNodes/PostProcessNode.h"

#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_glfw.h>

using namespace baamboo;

void RayTracingApp::Initialize(eRendererAPI api)
{
	m_DeviceSettings.bRaytracing = true;

	Super::Initialize(api);

	m_CameraController.SetLookAt(float3(0.0, 0.0f, -5.0f), float3(0.0f, 0.0f, 1.0f));
	m_pCamera = new EditorCamera(m_CameraController, m_pWindow->Width(), m_pWindow->Height());
}

void RayTracingApp::Update(float dt)
{
	Super::Update(dt);

	m_CameraController.Update(dt);
}

void RayTracingApp::Release()
{
	m_CameraController.Reset();

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
}

void RayTracingApp::ConfigureRenderGraph()
{
	m_pScene->AddRenderNode(MakeArc< StaticSkyboxNode >(*m_pRendererBackend->GetDevice()));
	m_pScene->AddRenderNode(MakeArc< RaytracingTestNode >(*m_pRendererBackend->GetDevice()));
	m_pScene->AddRenderNode(MakeArc< PostProcessNode >(*m_pRendererBackend->GetDevice()));
}

void RayTracingApp::ConfigureSceneObjects()
{
	// static mesh
	{
		MeshDescriptor descriptor = {};
		descriptor.rootPath          = GetModelPath();
		descriptor.bOptimize         = true;
		descriptor.rendererAPI       = s_RendererAPI;
		descriptor.bWindingCW        = true;
		descriptor.bGenerateMeshlets = false;

		auto entity = m_pScene->ImportModel(MODEL_PATH.append("DamagedHelmet/DamagedHelmet.gltf"), descriptor);
		entity.AttachComponent< ScriptComponent >();

		auto entity4 = m_pScene->ImportModel(MODEL_PATH.append("cube.obj"), descriptor);
		auto& tc4 = entity4.GetComponent< TransformComponent >();
		tc4.transform.position = float3(0.0f, 5.0f, 0.0f);

		auto entity3 = m_pScene->ImportModel(MODEL_PATH.append("cube.obj"), descriptor);
		entity3.AttachComponent< MaterialComponent >();
		auto& tc3 = entity3.GetComponent< TransformComponent >();
		tc3.transform.position = float3(0.0f, -5.0f, 0.0f);
		tc3.transform.scale = float3(10.0f, 1.0f, 10.0f);
	}
	// environment
	{
		auto environment = m_pScene->CreateEntity("Environment");
		auto& transformComponent = environment.GetComponent< TransformComponent >();
		transformComponent.transform.position = float3(-0.22427f, 0.84396f, -0.48726);

		auto& light = environment.AttachComponent< LightComponent >();
		light.type             = eLightType::Directional;
		light.color            = float3(1.0f, 0.95f, 0.8f);
		light.temperatureK     = 10000.0f;
		light.illuminanceLux   = 5.0f;
		light.angularRadiusRad = 0.00465f;

		auto& atmosphere = environment.AttachComponent< AtmosphereComponent >();
		atmosphere.skybox = TEXTURE_PATH.string() + "Skybox_Field.jpg";
	}
	// post-process volume
	{
		auto  volume = m_pScene->CreateEntity("PostProcessVolume");
		auto& pp     = volume.AttachComponent< PostProcessComponent >();

		pp.tonemap.op    = eToneMappingOp::ACES;
		pp.tonemap.ev100 = 0.0f;
		pp.tonemap.gamma = 1.0f;
	}
}
