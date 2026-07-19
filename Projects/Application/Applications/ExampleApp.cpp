#include "ExampleApp.h"

#include "BaambooCore/Common.h"
#include "BaambooCore/Window.h"
#include "BaambooCore/Input.hpp"

#include "BaambooScene/Entity.h"
#include "BaambooScene/Components.h"

#include "BaambooScene/RenderNodes/AtmosphereNode.h"
#include "BaambooScene/RenderNodes/GBufferNode.h"
#include "BaambooScene/RenderNodes/CullingNode.h"
#include "BaambooScene/RenderNodes/CloudNode.h"
#include "BaambooScene/RenderNodes/SkyboxNode.h"
#include "BaambooScene/RenderNodes/LightingNode.h"
#include "BaambooScene/RenderNodes/SurfaceResolveNode.h"
#include "BaambooScene/RenderNodes/PostProcessNode.h"

#include <cmath>
#include <glm/gtc/type_ptr.hpp>
#include <imgui/backends/imgui_impl_glfw.h>

using namespace baamboo;

void ExampleApp::Initialize(eRendererAPI api)
{
	m_DeviceSettings.bMeshShader = true;

	Super::Initialize(api);

	m_CameraController.SetLookAt(float3(0.0f, 0.0f, 0.0f), float3(0.0f, 0.0f, 1.0f));
	m_pCamera = new EditorCamera(m_CameraController, m_pWindow->Width(), m_pWindow->Height());
}

void ExampleApp::Update(f32 dt)
{
	Super::Update(dt);

	m_CameraController.Update(dt);
}

void ExampleApp::Release()
{
	m_CameraController.Reset();

	Super::Release();
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
			if (!io.WantCaptureKeyboard || action == GLFW_RELEASE)
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
			if (!io.WantCaptureMouse || action == GLFW_RELEASE)
			{
				if (!io.WantCaptureMouse)
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

	ConfigureRenderGraph();
	ConfigureSceneObjects();

	return m_pScene != nullptr;
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
			ImGui::InputFloat("##ClipNear", &m_pCamera->zNear, 0, 0, "%.2f");
			ImGui::PopItemWidth();

			ImGui::PushItemWidth(width * 0.7f);
			ImGui::SameLine();
			ImGui::InputFloat("##ClipFar", &m_pCamera->zFar, 0, 0, "%.2f");
			ImGui::PopItemWidth();

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

void ExampleApp::ConfigureRenderGraph()
{
	m_pScene->AddRenderNode(MakeArc< CloudShapeNode >(*m_pRendererBackend->GetDevice()));
	m_pScene->AddRenderNode(MakeArc< AtmosphereNode >(*m_pRendererBackend->GetDevice()));
	m_pScene->AddRenderNode(MakeArc< ClusterBuildNode >(*m_pRendererBackend->GetDevice()));
	m_pScene->AddRenderNode(MakeArc< LightCullingNode >(*m_pRendererBackend->GetDevice()));
	{
		auto pGBufferNode = MakeArc< GBufferNode >(*m_pRendererBackend->GetDevice());
		auto pCullingNode = MakeArc< CullingNode >(*m_pRendererBackend->GetDevice());
		pCullingNode->SetGBufferNode(pGBufferNode);
		m_pScene->AddRenderNode(pCullingNode);
	}
	m_pScene->AddRenderNode(MakeArc< SurfaceResolveNode >(*m_pRendererBackend->GetDevice()));
	m_pScene->AddRenderNode(MakeArc< CloudScatteringNode >(*m_pRendererBackend->GetDevice()));
	m_pScene->AddRenderNode(MakeArc< DynamicSkyboxNode >(*m_pRendererBackend->GetDevice()));
	m_pScene->AddRenderNode(MakeArc< LightingNode >(*m_pRendererBackend->GetDevice()));
	m_pScene->AddRenderNode(MakeArc< PostProcessNode >(*m_pRendererBackend->GetDevice()));
}

void ExampleApp::ConfigureSceneObjects()
{
	// static mesh
	{
		MeshDescriptor descriptor = {};
		descriptor.rootPath          = GetModelPath();
		descriptor.bOptimize         = true;
		descriptor.rendererAPI       = s_RendererAPI;
		descriptor.bWindingCW        = true;
		descriptor.bGenerateMeshlets = true;

		auto ground = m_pScene->ImportModel(MODEL_PATH.append("cube.obj"), descriptor);
		{
			auto& tc = ground.GetComponent< TransformComponent >();
			tc.transform.position = float3(0.0f, -1.5f, 480.0f);
			tc.transform.scale    = float3(800.0f, 0.25f, 500.0f);

			auto& m = ground.GetComponent< MaterialComponent >();
			m.tint      = float4(0.55f, 0.4f, 0.28f, 1.0f);
			m.roughness = 0.8f;
			m.metallic  = 0.0f;
		}

		constexpr i32 kSweepCols = 10; // metallic 0..1, left -> right
		constexpr i32 kSweepRows = 5;  // roughness 0..1, bottom -> top
		for (i32 row = 0; row < kSweepRows; ++row)
		{
			for (i32 col = 0; col < kSweepCols; ++col)
			{
				auto sphere = m_pScene->ImportModel(MODEL_PATH.append("_synthetic/icosphere_hq.ply"), descriptor);

				auto& tc = sphere.GetComponent< TransformComponent >();
				tc.transform.position = float3(-7.6f + 3.2f * col, 3.0f * row, 16.0f);

				auto& m = sphere.GetComponent< MaterialComponent >();
				m.tint      = float4(0.9f, 0.9f, 0.9f, 1.0f);
				m.roughness = float(row) / float(kSweepRows - 1);
				m.metallic  = float(col) / float(kSweepCols - 1);
			}
		}

		/*auto helmet = m_pScene->ImportModel(MODEL_PATH.append("DamagedHelmet/DamagedHelmet.gltf"), descriptor);
		{
			auto& tc = helmet.GetComponent< TransformComponent >();
			tc.transform.position = float3(0.0f, 0.5f, 10.0f);
			tc.transform.scale    = float3(2.0f);
		}*/
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
		auto environment = m_pScene->CreateEntity("Environment");
		environment.AttachComponent< LightComponent >();
		environment.AttachComponent< AtmosphereComponent >();
		environment.AttachComponent< CloudComponent >();

		auto& light = environment.GetComponent< LightComponent >();
		light.type             = eLightType::Directional;
		light.temperatureK     = 5778.0f; // physical sun; low-sun warmth comes from atmosphere transmittance
		light.color            = float3(1.0f, 1.0f, 1.0f);
		light.illuminanceLux   = 120'000.0f; // physical direct-sun illuminance; pairs with EV100 ~14.6 (Sunny-16)
		light.angularRadiusRad = 0.00465f;

		auto& transformComponent = environment.GetComponent< TransformComponent >();
		transformComponent.transform.position = float3(-0.22427f, 0.84396f, -0.48726);
		//transformComponent.transform.position = float3(0.0f, 1.0f, 0.0f);

		auto& atmosphere = environment.GetComponent< AtmosphereComponent >();
		atmosphere.atmosphereRadiusKm = 6420.0f;

		auto& cloud = environment.GetComponent< CloudComponent >();
		//cloud.uprezRatio   = eCloudUprezRatio::X4;
		cloud.blueNoiseTex = TEXTURE_PATH.string() + "BlueNoise_R_128x128x64.png";
		//cloud.blueNoiseTex = TEXTURE_PATH.string() + "BlueNoise_RG_128x128x64";
	}

	// sphere light
	{
		auto sphereLight = m_pScene->CreateEntity("Sphere Light");
		sphereLight.AttachComponent< LightComponent >();

		auto& light          = sphereLight.GetComponent< LightComponent >();
		light.type           = eLightType::Sphere;
		light.color          = float3(1.0f, 1.0f, 1.0f);
		light.temperatureK   = 5000.0f;
		light.radiusM        = 1.0;
		light.luminousFluxLm = 100.0f;

		auto& transformComponent              = sphereLight.GetComponent< TransformComponent >();
		transformComponent.transform.position = float3(0.0f, 0.0f, 5.0f);
	}

	// Create a spot light
	{
		//auto spotLight = m_pScene->CreateEntity("Spot Light");
		//spotLight.AttachComponent <LightComponent >();

		//auto& light              = spotLight.GetComponent< LightComponent >();
		//light.type               = eLightType::Spot;
		//light.color              = float3(1.0f, 1.0f, 1.0f);
		//light.temperatureK      = 3200.0f;
		//light.radiusM           = 0.05f;
		//light.luminousPower_lm   = 25.0f * 10.0f;
		//light.outerConeAngleRad = PI_DIV(2.0f);

		//auto& transform              = spotLight.GetComponent< TransformComponent >();
		//transform.transform.position = float3(0.0f, 10.0f, 0.0f);
		//transform.transform.rotation = float3(90.0f, 0.0f, 0.0f); // Point down
	}

	// post-process volume
	{
		auto  volume = m_pScene->CreateEntity("PostProcessVolume");
		auto& pp = volume.AttachComponent< PostProcessComponent >();

		pp.effectBits   |= (1ull << ePostProcess::Bloom);
		pp.effectBits   |= (1ull << ePostProcess::AntiAliasing); // TAA (+ jittered projection)
		pp.tonemap.op    = eToneMappingOp::Uchimura;
		pp.tonemap.ev100 = 14.6f; // Sunny-16 for the 120k lux sun
		pp.tonemap.gamma = 2.2f;
	}
}
