#include "BistroApp.h"
#include "BaambooCore/Common.h"
#include "BaambooCore/Window.h"
#include "BaambooCore/Input.hpp"
#include "BaambooScene/Entity.h"
#include "BaambooScene/Components.h"
#include "BaambooScene/RenderNodes/AtmosphereNode.h"
#include "BaambooScene/RenderNodes/GBufferNode.h"
#include "BaambooScene/RenderNodes/CloudNode.h"
#include "BaambooScene/RenderNodes/LightingNode.h"
#include "BaambooScene/RenderNodes/PostProcessNode.h"

#include <glm/gtc/type_ptr.hpp>
#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_glfw.h>

using namespace baamboo;

void BistroApp::Initialize(eRendererAPI api)
{
	Super::Initialize(api);

	m_CameraController.SetLookAt(float3(0.0f, 0.0f, 0.0f), float3(0.0f));
	m_pCamera = new EditorCamera(m_CameraController, m_pWindow->Width(), m_pWindow->Height());
}

void BistroApp::Update(f32 dt)
{
	Super::Update(dt);

	m_CameraController.Update(dt);
}

void BistroApp::Release()
{
	m_CameraController.Reset();

	Super::Release();
}

bool BistroApp::InitWindow()
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
				auto app = reinterpret_cast<BistroApp*>(glfwGetWindowUserPointer(window));
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
			auto app = reinterpret_cast<BistroApp*>(glfwGetWindowUserPointer(window));
			if (app)
			{
				app->m_bWindowResized = true;
				app->m_ResizeWidth = width;
				app->m_ResizeHeight = height;
			}
		});

	m_pWindow->SetIconifyCallback([](GLFWwindow* window, i32 iconified)
		{
			auto app = reinterpret_cast<BistroApp*>(glfwGetWindowUserPointer(window));
			if (app)
			{
				app->m_bWindowResized = true;
				app->m_ResizeWidth = iconified ? 0 : app->m_pWindow->Width();
				app->m_ResizeHeight = iconified ? 0 : app->m_pWindow->Height();
			}
		});

	m_pWindow->SetCharCallback([](GLFWwindow* window, u32 c)
		{
			ImGui_ImplGlfw_CharCallback(window, c);
		});

	return true;
}

bool BistroApp::LoadScene()
{
	m_pScene = new Scene("BistroScene");

	ConfigureRenderGraph();
	ConfigureSceneObjects();

	return true;
}

void BistroApp::DrawUI()
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

			if (ImGui::BeginCombo("##Scale", "Movement Scale"))
			{
				if (ImGui::Selectable("cm", cameraConfig.movementScale == 1.0f))
				{
					cameraConfig.movementScale = 1.0f;
				}
				if (ImGui::Selectable("m", cameraConfig.movementScale == 100.0f))
				{
					cameraConfig.movementScale = 100.0f;
				}
				if (ImGui::Selectable("km", cameraConfig.movementScale == 100'000.0f))
				{
					cameraConfig.movementScale = 100'000.0f;
				}

				ImGui::EndCombo();
			}
		}
	}
	ImGui::End();
}

void BistroApp::ConfigureRenderGraph()
{
	m_pScene->AddRenderNode(MakeArc< GBufferNode >(*m_pRendererBackend->GetDevice()));
	m_pScene->AddRenderNode(MakeArc< LightingNode >(*m_pRendererBackend->GetDevice()));
	m_pScene->AddRenderNode(MakeArc< PostProcessNode >(*m_pRendererBackend->GetDevice()));
}

void BistroApp::ConfigureSceneObjects()
{
	// static mesh
	{
		MeshDescriptor descriptor = {};
		descriptor.rootPath = GetModelPath();
		descriptor.bOptimize = true;
		descriptor.rendererAPI = s_RendererAPI;
		descriptor.bWindingCW = true;

		auto dhEntity = m_pScene->ImportModel(MODEL_PATH.append("DamagedHelmet/DamagedHelmet.gltf"), descriptor);
		auto& tcdh = dhEntity.GetComponent< TransformComponent >();
		tcdh.transform.position = { -1.0f, 0.0f, 0.0f };

		auto dhEntity2 = m_pScene->ImportModel(MODEL_PATH.append("DamagedHelmet/DamagedHelmet.gltf"), descriptor);
		tcdh.transform.position = { 1.0f, 0.0f, 0.0f };
	}

	// animated mesh
	{
		/*MeshDescriptor descriptor  = {};
		descriptor.rootPath        = GetModelPath();
		descriptor.scale           = 10000.0f;
		descriptor.rendererAPI     = m_eBackendAPI;
		descriptor.bLoadAnimations = true;

		fs::path animatedModelPath = MODEL_PATH.append("woman/Woman.gltf");
		if (fs::exists(animatedModelPath))
		{
			Entity character = m_pScene->ImportModel(animatedModelPath, descriptor);

			auto& transformComponent              = character.GetComponent< TransformComponent >();
			transformComponent.transform.position = float3(0.0f, 0.0f, 5.0f);

			if (character.HasAll< AnimationComponent >())
			{
				auto& animComp         = character.GetComponent< AnimationComponent >();
				animComp.bPlaying      = true;
				animComp.bLoop         = true;
				animComp.playbackSpeed = 1.0f;
			}
		}*/
	}

	{
		auto sunLight = m_pScene->CreateEntity("Sun Light");
		sunLight.AttachComponent< LightComponent >();

		auto& light = sunLight.GetComponent< LightComponent >();
		light.type = eLightType::Directional;
		light.temperature_K = 10000.0f;
		light.color = float3(1.0f, 0.95f, 0.8f);
		light.illuminance_lux = 6.0f; //120'000.0f;
		light.angularRadius_rad = 0.00465f;

		auto& transformComponent = sunLight.GetComponent< TransformComponent >();
		//transformComponent.transform.position = float3(-0.46144, 0.76831, -0.44359);
		transformComponent.transform.position = float3(0.0f, 1.0f, 0.0f);
	}

	// Create a point light
	{
		/*auto pointLight = m_pScene->CreateEntity("Point Light");
		pointLight.AttachComponent< LightComponent >();

		auto& light            = pointLight.GetComponent< LightComponent >();
		light.type             = eLightType::Point;
		light.color            = float3(1.0f, 1.0f, 1.0f);
		light.temperature_K    = 4000.0f;
		light.radius_m         = 0.03f;
		light.luminousPower_lm = 1000.0f;

		auto& transformComponent              = pointLight.GetComponent< TransformComponent >();
		transformComponent.transform.position = float3(0.0f, 0.0f, 5.0f);*/
	}

	// Create a spot light
	{
		//auto spotLight = m_pScene->CreateEntity("Spot Light");
		//spotLight.AttachComponent <LightComponent >();

		//auto& light              = spotLight.GetComponent< LightComponent >();
		//light.type               = eLightType::Spot;
		//light.color              = float3(1.0f, 1.0f, 1.0f);
		//light.temperature_K      = 3200.0f;
		//light.radius_m           = 0.05f;
		//light.luminousPower_lm   = 25.0f * 10.0f;
		//light.outerConeAngle_rad = PI_DIV(2.0f);

		//auto& transform              = spotLight.GetComponent< TransformComponent >();
		//transform.transform.position = float3(0.0f, 10.0f, 0.0f);
		//transform.transform.rotation = float3(90.0f, 0.0f, 0.0f); // Point down
	}

	// post-process volume
	{
		auto  volume = m_pScene->CreateEntity("PostProcessVolume");
		auto& pp = volume.AttachComponent< PostProcessComponent >();

		pp.tonemap.op = eToneMappingOp::ACES;
		pp.tonemap.ev100 = 0.0f;
		pp.tonemap.gamma = 2.2f;
	}
}