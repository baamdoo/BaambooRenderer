#include "ExampleApp.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "BaambooCore/Window.h"
#include "BaambooCore/Input.hpp"
#include "BaambooUtils/ModelLoader.h"

#include <glm/gtc/type_ptr.hpp>
#include <imgui/backends/imgui_impl_glfw.h>

using namespace baamboo;

void ExampleApp::Initialize(eRendererAPI api)
{
	Super::Initialize(api);

	m_CameraController.SetLookAt(float3(0.0f, 0.0f, -5.0f), float3(0.0f));
	m_pCamera = new EditorCamera(m_CameraController, m_pWindow->Width(), m_pWindow->Height());
}

void ExampleApp::Update(f32 dt)
{
	Super::Update(dt);

	m_CameraController.Update(dt);
}

bool ExampleApp::InitWindow()
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
				auto app = reinterpret_cast<ExampleApp*>(glfwGetWindowUserPointer(window));
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
			auto app = reinterpret_cast<ExampleApp*>(glfwGetWindowUserPointer(window));
			if (app)
			{
				app->m_bWindowResized = true;
				app->m_ResizeWidth = width;
				app->m_ResizeHeight = height;
			}
		});

	m_pWindow->SetIconifyCallback([](GLFWwindow* window, i32 iconified)
		{
			auto app = reinterpret_cast<ExampleApp*>(glfwGetWindowUserPointer(window));
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

bool ExampleApp::LoadScene()
{
	m_pScene = new Scene("ExampleScene");
	auto entity0 = m_pScene->CreateEntity("Test0");
	auto& tc0 = entity0.GetComponent< TransformComponent >();
	tc0.transform.position = { 0.0f, 1.0f, 0.0f };

	auto entity00 = m_pScene->CreateEntity("Test00");
	entity0.AttachChild(entity00);

	auto entity1 = m_pScene->CreateEntity("Test1");

	MeshDescriptor meshDescriptor = {};
	meshDescriptor.bOptimize = true;
	meshDescriptor.rendererAPI = m_eBackendAPI;

	auto dhEntity = m_pScene->ImportModel(MODEL_PATH.append("DamagedHelmet/DamagedHelmet.gltf"), meshDescriptor);
	auto& tcdh = dhEntity.GetComponent< TransformComponent >();
	tcdh.transform.position = { -1.0f, 0.0f, 0.0f };

	auto dhEntity2 = m_pScene->ImportModel(MODEL_PATH.append("DamagedHelmet/DamagedHelmet.gltf"), meshDescriptor);
	return true;
}

void ExampleApp::DrawUI()
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
			ImGui::InputFloat("##ClipNear", &m_pCamera->cNear, 0, 0, "%.2f");

			ImGui::PushItemWidth(width * 0.7f);
			ImGui::SameLine();
			ImGui::InputFloat("##ClipFar", &m_pCamera->cFar, 0, 0, "%.2f");

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
