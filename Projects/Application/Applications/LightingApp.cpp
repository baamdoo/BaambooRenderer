#include "LightingApp.h"

#include "BaambooCore/Common.h"
#include "BaambooCore/Window.h"
#include "BaambooCore/Input.hpp"

#include "BaambooScene/Entity.h"
#include "BaambooScene/Components.h"
#include "BaambooScene/RenderNodes/SkyboxNode.h"
#include "BaambooScene/RenderNodes/GBufferNode.h"
#include "BaambooScene/RenderNodes/CullingNode.h"
#include "BaambooScene/RenderNodes/LightingNode.h"
#include "BaambooScene/RenderNodes/SurfaceResolveNode.h"
#include "BaambooScene/RenderNodes/DebugDrawNode.h"
#include "BaambooScene/RenderNodes/PostProcessNode.h"

#include <glm/gtc/type_ptr.hpp>
#include <imgui/backends/imgui_impl_glfw.h>

using namespace baamboo;

void LightingApp::Initialize(eRendererAPI api)
{
	m_DeviceSettings.bMeshShader = true;

	Super::Initialize(api);

	m_CameraController.SetLookAt(float3(0.0, 0.0f, -5.0f), float3(0.0f, 0.0f, 1.0f));
	m_pCamera = new EditorCamera(m_CameraController, m_pWindow->Width(), m_pWindow->Height());
}

void LightingApp::Update(f32 dt)
{
	Super::Update(dt);

	m_CameraController.Update(dt);
}

void LightingApp::Release()
{
	m_CameraController.Reset();

	Super::Release();
}

bool LightingApp::InitWindow()
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
				auto app = reinterpret_cast<LightingApp*>(glfwGetWindowUserPointer(window));
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
			auto app = reinterpret_cast<LightingApp*>(glfwGetWindowUserPointer(window));
			if (app)
			{
				app->m_bWindowResized = true;
				app->m_ResizeWidth = width;
				app->m_ResizeHeight = height;
			}
		});

	m_pWindow->SetIconifyCallback([](GLFWwindow* window, i32 iconified)
		{
			auto app = reinterpret_cast<LightingApp*>(glfwGetWindowUserPointer(window));
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

bool LightingApp::LoadScene()
{
	m_pScene = new Scene("BistroScene");

	ConfigureRenderGraph();
	ConfigureSceneObjects();

	return m_pScene != nullptr;
}

void LightingApp::DrawUI()
{
	Super::DrawUI();

	if (m_pScene)
	{
		ImGui::Begin("Light Cluster Visualization");
		{
			// --- Cluster Wireframe ---
			if (ImGui::CollapsingHeader("Cluster Wireframe"))
			{
				bool bShowCluster = m_pScene->GetDebugClusterWireframe();
				if (ImGui::Checkbox("Show clusters", &bShowCluster))
					m_pScene->SetDebugClusterWireframe(bShowCluster);

				bool bHeatmap = m_pScene->GetDebugClusterHeatmap();
				if (ImGui::Checkbox("Heatmap (light count)", &bHeatmap))
					m_pScene->SetDebugClusterHeatmap(bHeatmap);

				bool bSkipEmpty = m_pScene->GetDebugSkipEmpty();
				if (ImGui::Checkbox("Skip empty clusters", &bSkipEmpty))
					m_pScene->SetDebugSkipEmpty(bSkipEmpty);

				int saturationMax = (int)m_pScene->GetDebugSaturationMax();
				if (ImGui::SliderInt("Saturation (count)", &saturationMax, 1, 64))
					m_pScene->SetDebugSaturationMax((u32)saturationMax);

				ImGui::TextUnformatted("Turbo");
				ImGui::SameLine();
				const float legendWidth = 200.0f;
				const float legendHeight = 16.0f;
				ImVec2 p0 = ImGui::GetCursorScreenPos();
				ImDrawList* dl = ImGui::GetWindowDrawList();
				constexpr int N = 32;
				auto turbo = [](float t) -> ImU32
				{
					t = std::clamp(t, 0.0f, 1.0f);
					float t2 = t * t, t3 = t2 * t, t4 = t3 * t, t5 = t4 * t;
					float r = 0.13572138f + 4.61539260f * t  - 42.66032258f * t2 + 132.13108234f * t3 - 152.94239396f * t4 + 59.28637943f * t5;
					float g = 0.09140261f + 2.19418839f * t  +  4.84296658f * t2 -  14.18503333f * t3 +   4.27729857f * t4 +  2.82956604f * t5;
					float b = 0.10667330f + 12.64194608f * t - 60.58204836f * t2 + 110.36276771f * t3 -  89.90310912f * t4 + 27.34824973f * t5;
					auto sat = [](float x){ return std::clamp(x, 0.0f, 1.0f); };
					return IM_COL32(int(sat(r)*255), int(sat(g)*255), int(sat(b)*255), 255);
				};
				for (int i = 0; i < N; ++i)
				{
					float t0 = float(i) / float(N);
					float t1 = float(i + 1) / float(N);
					ImVec2 a(p0.x + t0 * legendWidth, p0.y);
					ImVec2 b(p0.x + t1 * legendWidth, p0.y + legendHeight);
					dl->AddRectFilled(a, b, turbo((t0 + t1) * 0.5f));
				}
				ImGui::Dummy(ImVec2(legendWidth, legendHeight));
				ImGui::TextDisabled("0   .25   .50   .75   sat+");
			}

			ImGui::Separator();

			// --- Light Wireframes ---
			if (ImGui::CollapsingHeader("Light Wireframes"))
			{
				u32 mask = m_pScene->GetDebugLightTypeMask();

				auto toggleBit = [&](const char* label, u32 bit) {
					bool on = (mask & (1u << bit)) != 0u;
					if (ImGui::Checkbox(label, &on))
					{
						mask = on ? (mask | (1u << bit)) : (mask & ~(1u << bit));
						m_pScene->SetDebugLightTypeMask(mask);
					}
				};
				toggleBit("Spot",   0);
				toggleBit("Area",   1);
				toggleBit("Sphere", 2);
				toggleBit("Disk",   3);
				toggleBit("Tube",   4);
				ImGui::TextDisabled("(Directional has infinite influence — no gizmo)");
			}
		}
		ImGui::End();
	}

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

void LightingApp::ConfigureRenderGraph()
{
	m_pScene->AddRenderNode(MakeArc< StaticSkyboxNode >(*m_pRendererBackend->GetDevice()));
	{
		auto pGBufferNode = MakeArc< GBufferNode >(*m_pRendererBackend->GetDevice());
		auto pCullingNode = MakeArc< CullingNode >(*m_pRendererBackend->GetDevice());
		pCullingNode->SetGBufferNode(pGBufferNode);
		m_pScene->AddRenderNode(pCullingNode);
	}
	m_pScene->AddRenderNode(MakeArc< ClusterBuildNode >(*m_pRendererBackend->GetDevice()));
	m_pScene->AddRenderNode(MakeArc< LightCullingNode >(*m_pRendererBackend->GetDevice()));
	m_pScene->AddRenderNode(MakeArc< SurfaceResolveNode >(*m_pRendererBackend->GetDevice()));
	m_pScene->AddRenderNode(MakeArc< LightingNode >(*m_pRendererBackend->GetDevice()));
	m_pScene->AddRenderNode(MakeArc< DebugDrawNode >(*m_pRendererBackend->GetDevice()));
	m_pScene->AddRenderNode(MakeArc< PostProcessNode >(*m_pRendererBackend->GetDevice()));
}

void LightingApp::ConfigureSceneObjects()
{
	// static mesh
	{
		MeshDescriptor descriptor = {};
		descriptor.rootPath = GetModelPath();
		descriptor.bOptimize = true;
		descriptor.rendererAPI = s_RendererAPI;
		descriptor.bWindingCW = true;
		descriptor.bGenerateMeshlets = true;
		descriptor.numLODs = 8;

		auto entity = m_pScene->ImportModel(MODEL_PATH.append("DamagedHelmet/DamagedHelmet.gltf"), descriptor);
		entity.AttachComponent< ScriptComponent >();

		const float3 scaleFloor = float3(10.0f, 0.4f, 10.0f);
		const float3 scaleSide  = float3(0.4f, 10.0f, 10.0f);
		const float3 scaleEnd   = float3(10.0f, 10.0f, 0.4f);

		auto floor = m_pScene->ImportModel(MODEL_PATH.append("cube.obj"), descriptor);
		floor.GetComponent< TransformComponent >().transform.position = float3(0.0f, -10.0f, 0.0f);
		floor.GetComponent< TransformComponent >().transform.scale = scaleFloor;

		auto ceiling = m_pScene->ImportModel(MODEL_PATH.append("cube.obj"), descriptor);
		ceiling.GetComponent< TransformComponent >().transform.position = float3(0.0f, 10.0f, 0.0f);
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

		auto& m = backWall.GetComponent< MaterialComponent >();
		m.roughness = 1.0f;

		/*auto frontWall = m_pScene->ImportModel(MODEL_PATH.append("cube.obj"), descriptor);
		frontWall.GetComponent< TransformComponent >().transform.position = float3(0.0f, 0.0f, -10.0f);
		frontWall.GetComponent< TransformComponent >().transform.scale = scaleEnd;*/
	}

	{
		auto sunLight = m_pScene->CreateEntity("Skybox");
		sunLight.AttachComponent< AtmosphereComponent >();

		auto& atmosphere = sunLight.GetComponent< AtmosphereComponent >();
		atmosphere.skybox = TEXTURE_PATH.string() + "Skybox_Field.jpg";
	}

	// sphere light — front-left corner, pink
	{
		auto sphereLight = m_pScene->CreateEntity("Sphere Light");
		sphereLight.AttachComponent< LightComponent >();

		auto& light = sphereLight.GetComponent< LightComponent >();
		light.type           = eLightType::Sphere;
		light.color          = float3(1.0f, 0.324f, 0.674f);
		light.temperatureK   = 10000.0f;
		light.radiusM        = 0.5f;
		light.luminousFluxLm = 3000.0f;

		auto& transformComponent = sphereLight.GetComponent< TransformComponent >();
		transformComponent.transform.position = float3(-5.0f, 5.0f, -3.0f);
	}

	// area light — ceiling, cool blue, single-sided down-emitting
	{
		auto areaLight = m_pScene->CreateEntity("Area Light");
		areaLight.AttachComponent< LightComponent >();

		auto& light = areaLight.GetComponent< LightComponent >();
		light.type           = eLightType::Area;
		light.color          = float3(0.235f, 0.678f, 0.988f);
		light.temperatureK   = 10000.0f;
		light.luminousFluxLm = 1500.0f;

		auto& transformComponent = areaLight.GetComponent< TransformComponent >();
		// -Z forward convention: X axis -90 deg pitch puts emit direction (-Z) onto -Y (ceiling-down).
		transformComponent.transform.position = float3(0.0f, 7.0f, 0.0f);
		transformComponent.transform.rotation = float3(90.0f, 0.0f, 0.0f);
		transformComponent.transform.scale    = float3(1.0f, 1.0f, 1.0f);
	}

	// disk light — right side, magenta
	{
		auto diskLight = m_pScene->CreateEntity("Disk Light");
		diskLight.AttachComponent< LightComponent >();

		auto& light = diskLight.GetComponent< LightComponent >();
		light.type           = eLightType::Disk;
		light.color          = float3(0.988f, 0.235f, 0.678f);
		light.temperatureK   = 0.0f;
		light.luminousFluxLm = 1500.0f;
		light.diskRadiusM    = 1.0f;

		auto& transformComponent = diskLight.GetComponent< TransformComponent >();
		transformComponent.transform.position = float3(5.0f, 5.0f, 0.0f);
		transformComponent.transform.rotation = float3(90.0f, 0.0f, 0.0f);
		transformComponent.transform.scale    = float3(1.0f, 1.0f, 1.0f);
	}

	// spot light — back-right, downward, warm tungsten
	{
		auto spotLight = m_pScene->CreateEntity("Spot Light");
		spotLight.AttachComponent< LightComponent >();

		auto& light = spotLight.GetComponent< LightComponent >();
		light.type              = eLightType::Spot;
		light.color             = float3(1.0f, 0.85f, 0.65f);
		light.temperatureK      = 3200.0f;
		light.radiusM           = 0.05f;
		light.luminousFluxLm    = 800.0f;
		light.innerConeAngleRad = PI_DIV(8.0f);
		light.outerConeAngleRad = PI_DIV(4.0f);

		auto& transform              = spotLight.GetComponent< TransformComponent >();
		transform.transform.position = float3(3.0f, 8.0f, 5.0f);
		transform.transform.rotation = float3(60.0f, 0.0f, 0.0f); // pitch downward
	}

	// tube light — back-left ceiling, neutral white
	{
		auto tubeLight = m_pScene->CreateEntity("Tube Light");
		tubeLight.AttachComponent< LightComponent >();

		auto& light = tubeLight.GetComponent< LightComponent >();
		light.type           = eLightType::Tube;
		light.color          = float3(1.0f, 1.0f, 1.0f);
		light.temperatureK   = 6500.0f;
		light.luminousFluxLm = 1200.0f;
		light.tubeLengthM    = 4.0f;
		light.tubeRadiusM    = 0.05f;

		auto& transformComponent = tubeLight.GetComponent< TransformComponent >();
		transformComponent.transform.position = float3(-4.0f, 8.0f, 4.0f);
		transformComponent.transform.rotation = float3(0.0f, 0.0f, 90.0f); // align along X axis
		transformComponent.transform.scale    = float3(1.0f, 1.0f, 1.0f);
	}

	// post-process volume
	{
		auto  volume = m_pScene->CreateEntity("PostProcessVolume");
		auto& pp = volume.AttachComponent< PostProcessComponent >();

		pp.tonemap.op    = eToneMappingOp::ACES;
		pp.tonemap.ev100 = 0.0f;
		pp.tonemap.gamma = 2.2f;
	}
}
