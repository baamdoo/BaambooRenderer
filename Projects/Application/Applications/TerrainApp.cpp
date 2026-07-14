#include "TerrainApp.h"

#include "BaambooCore/Common.h"
#include "BaambooCore/Window.h"
#include "BaambooCore/Input.hpp"

#include "BaambooScene/Entity.h"
#include "BaambooScene/Components.h"
#include "BaambooScene/RenderNodes/GBufferNode.h"
#include "BaambooScene/RenderNodes/CullingNode.h"
#include "BaambooScene/RenderNodes/VoxelChunkRenderNode.h"
#include "BaambooScene/RenderNodes/SkyboxNode.h"
#include "BaambooScene/RenderNodes/LightingNode.h"
#include "BaambooScene/RenderNodes/SurfaceResolveNode.h"
#include "BaambooScene/RenderNodes/DebugDrawNode.h"
#include "BaambooScene/RenderNodes/PostProcessNode.h"
#include "BaambooScene/Systems/TransformSystem.h"
#include "BaambooScene/Systems/VoxelTerrainSystem.h"

#include <algorithm>
#include <cstdio>
#include <string>

#include <glm/gtc/type_ptr.hpp>
#include <imgui/backends/imgui_impl_glfw.h>

using namespace baamboo;

namespace
{

constexpr const char* kVoxelTerrainRootTag = "VoxelTerrainRoot";

} // namespace

void TerrainApp::Initialize(eRendererAPI api)
{
	m_DeviceSettings.bMeshShader = true;

	Super::Initialize(api);

	m_CameraController.SetLookAt(float3(192.0f, 128.0f, -192.0f), float3(64.0f, 64.0f, 64.0f));
	m_pCamera = new EditorCamera(m_CameraController, m_pWindow->Width(), m_pWindow->Height());
	m_pCamera->zNear = 0.1f;
	m_pCamera->zFar  = 1000.0f;
}

void TerrainApp::Update(f32 dt)
{
	Super::Update(dt);

	m_CameraController.Update(dt);
}

void TerrainApp::Release()
{
	m_CameraController.Reset();

	Super::Release();
}

bool TerrainApp::InitWindow()
{
	WindowDescriptor windowDesc = { .numDesiredImages = 3, .bVSync = false };
	m_pWindow = new Window(windowDesc);

	glfwSetWindowUserPointer(m_pWindow->Handle(), this);
	m_pWindow->SetKeyCallback([](GLFWwindow* window, i32 key, i32 scancode, i32 action, i32 mods)
		{
			ImGui_ImplGlfw_KeyCallback(window, key, scancode, action, mods);

			ImGuiIO& io = ImGui::GetIO();
			if (!io.WantCaptureKeyboard)
			{
				auto app = reinterpret_cast<TerrainApp*>(glfwGetWindowUserPointer(window));
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
				bool bPressed = action != GLFW_RELEASE;
				Input::Inst()->UpdateMouse(button, bPressed);
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
			auto app = reinterpret_cast<TerrainApp*>(glfwGetWindowUserPointer(window));
			if (app)
			{
				app->m_bWindowResized = true;
				app->m_ResizeWidth = width;
				app->m_ResizeHeight = height;
			}
		});

	m_pWindow->SetIconifyCallback([](GLFWwindow* window, i32 iconified)
		{
			auto app = reinterpret_cast<TerrainApp*>(glfwGetWindowUserPointer(window));
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

bool TerrainApp::LoadScene()
{
	m_pScene = new Scene("TerrainScene");

	ConfigureRenderGraph();
	ConfigureSceneObjects();

	return m_pScene != nullptr;
}

void TerrainApp::DrawUI()
{
	Super::DrawUI();

	ImGui::Begin("Voxel Terrain");
	{
		if (!m_VoxelTerrainRootEntity.IsValid() || !m_VoxelTerrainRootEntity.HasAll< VoxelTerrainComponent >())
		{
			ImGui::TextDisabled("Voxel terrain root component is missing.");
		}
		else
		{
			auto& terrain = m_VoxelTerrainRootEntity.GetComponent< VoxelTerrainComponent >();
			bool bRebuildChunk = false;

			ImGui::DragFloat3("Terrain Origin", glm::value_ptr(terrain.terrainOriginWorld), 0.5f, 0.0f, 0.0f, "%.2f m");
			if (ImGui::IsItemDeactivatedAfterEdit())
				bRebuildChunk = true;

			if (ImGui::Button("Rebuild"))
				bRebuildChunk = true;

			if (ImGui::CollapsingHeader("Procedural Surface", ImGuiTreeNodeFlags_DefaultOpen))
			{
				auto& gs = terrain.settings;
				int octaves = (int)gs.octaves;
				int seed    = (int)gs.seed;

				// Rebuild on change.
				bRebuildChunk |= ImGui::SliderFloat("Detail Weight",  &gs.detailWeight,      0.0f, 4.0f,  "%.2f");
				bRebuildChunk |= ImGui::SliderFloat("Ridged Blend",   &gs.ridgedBlend,       0.0f, 1.0f,  "%.2f");
				bRebuildChunk |= ImGui::SliderFloat("Redistribution", &gs.redistributionExp, 0.2f, 4.0f,  "%.2f");
				bRebuildChunk |= ImGui::SliderInt  ("Octaves",        &octaves,              1,    8           );
				bRebuildChunk |= ImGui::SliderFloat("Frequency",      &gs.frequency,         0.005f, 0.2f, "%.4f");
				bRebuildChunk |= ImGui::SliderFloat("Lacunarity",     &gs.lacunarity,        1.5f, 3.0f,  "%.2f");
				bRebuildChunk |= ImGui::SliderFloat("Gain",           &gs.gain,              0.2f, 0.8f,  "%.2f");
				bRebuildChunk |= ImGui::SliderFloat("Warp Strength",  &gs.warpStrength,      0.0f, 3.0f,  "%.2f");
				bRebuildChunk |= ImGui::SliderFloat("Warp Frequency", &gs.warpFrequency,     0.005f, 0.1f, "%.4f");
				bRebuildChunk |= ImGui::SliderFloat("Amplitude (m)",  &gs.mountainAmplitude, 0.0f, 64.0f, "%.1f");
				bRebuildChunk |= ImGui::SliderFloat("Surface Level",  &gs.surfaceLevelRatio,  0.0f, 1.0f,  "%.2f");
				bRebuildChunk |= ImGui::InputInt   ("Seed",           &seed);

				gs.octaves = (u32)(octaves < 1 ? 1 : octaves);
				gs.seed    = (u32)(seed    < 0 ? 0 : seed);
			}

			if (ImGui::CollapsingHeader("Erosion", ImGuiTreeNodeFlags_DefaultOpen))
			{
				auto& gs = terrain.settings;
				int erosionOctaves = (int)gs.erosionOctaves;

				// Rebuild on change.
				bRebuildChunk |= ImGui::SliderInt  ("Ero Octaves",     &erosionOctaves,           0,     8           );
				bRebuildChunk |= ImGui::SliderFloat("Ero Scale (m)",   &gs.erosionScale,          5.0f,  48.0f, "%.1f");
				bRebuildChunk |= ImGui::SliderFloat("Ero Strength",    &gs.erosionStrength,       0.0f,  1.0f,  "%.2f");
				bRebuildChunk |= ImGui::SliderFloat("Gully Weight",    &gs.erosionGullyWeight,    0.0f,  1.0f,  "%.2f");
				bRebuildChunk |= ImGui::SliderFloat("Ero Detail",      &gs.erosionDetail,         0.5f,  3.0f,  "%.2f");
				bRebuildChunk |= ImGui::SliderFloat("Cell Scale",      &gs.erosionCellScale,      0.4f,  1.0f,  "%.2f");
				bRebuildChunk |= ImGui::SliderFloat("Normalization",   &gs.erosionNormalization,  0.0f,  1.0f,  "%.2f");
				bRebuildChunk |= ImGui::SliderFloat("Slope Scale",     &gs.erosionSlopeScale,     0.0f,  4.0f,  "%.2f");
				// Higher onset restricts carving to steeper slopes.
				bRebuildChunk |= ImGui::SliderFloat("Onset Input",     &gs.erosionOnsetInput,     0.25f, 4.0f,  "%.2f");
				bRebuildChunk |= ImGui::SliderFloat("Onset Octave",    &gs.erosionOnsetOctave,    0.25f, 4.0f,  "%.2f");

				gs.erosionOctaves = (u32)(erosionOctaves < 0 ? 0 : erosionOctaves);
			}

			if (ImGui::CollapsingHeader("Micro Dicing", ImGuiTreeNodeFlags_DefaultOpen))
			{
				auto& dice = terrain.settings.dice;
				int  diceMaxLevel = (int)dice.maxLevel;
				bool bLevelTint   = (dice.debugFlags & 1u) != 0u;

				// Live, no rebuild.
				ImGui::SliderInt  ("Dice Max Level",  &diceMaxLevel,           0,    5           );
				ImGui::SliderFloat("Target Px",       &dice.targetPx,          2.0f, 16.0f, "%.1f");
				ImGui::SliderFloat("Dice Radius (m)", &dice.radiusM,           5.0f, 80.0f, "%.0f");
				ImGui::SliderFloat("Dice Fade (m)",   &dice.fadeWidthMeter,        1.0f, 20.0f, "%.0f");
				ImGui::SliderFloat("Disp Scale",      &dice.displacementScale, 0.0f, 2.0f,  "%.2f");
				if (ImGui::Checkbox("Level Tint", &bLevelTint))
					dice.debugFlags = bLevelTint ? (dice.debugFlags | 1u) : (dice.debugFlags & ~1u);

				int microOctaves = (int)dice.microOctaves;
				ImGui::SliderFloat("Micro Amp (m)",      &dice.microAmplitudeMeter,        0.0f,  0.10f, "%.3f");
				ImGui::SliderFloat("Micro Base WL (m)",  &dice.microBaseWaveLengthMeter,     0.05f, 1.0f,  "%.2f");
				ImGui::SliderFloat("Micro Lacunarity",   &dice.microLacunarity,  1.5f,  4.0f,  "%.2f");
				ImGui::SliderFloat("Micro Gain",         &dice.microGain,        0.1f,  0.9f,  "%.2f");
				ImGui::SliderFloat("Micro Sharpness",    &dice.microSharpness,  -1.0f,  1.0f,  "%.2f"); // -1 ridged .. +1 billowed
				ImGui::SliderFloat("Micro Crease Boost", &dice.microCreaseBoost, 0.0f,  4.0f,  "%.2f");
				ImGui::SliderInt  ("Micro Octaves",      &microOctaves,          0,     6     );

				bool bMicroCavity = (dice.debugFlags & 4u) != 0u;
				if (ImGui::Checkbox("Micro Cavity", &bMicroCavity))
					dice.debugFlags = bMicroCavity ? (dice.debugFlags | 4u) : (dice.debugFlags & ~4u);

				dice.maxLevel     = (u32)(diceMaxLevel < 0 ? 0 : diceMaxLevel);
				dice.microOctaves = (u32)(microOctaves < 0 ? 0 : microOctaves);
			}

			if (bRebuildChunk)
				m_pScene->Registry().patch< VoxelTerrainComponent >(m_VoxelTerrainRootEntity.ID(), [](auto&) {});

			ImGui::Separator();
			ImGui::Text("Chunk Size      %.1f m", terrain.settings.chunkWorldSizeMeter);
			ImGui::Text("Cells / Axis    %u", terrain.settings.cellsPerAxis);
			ImGui::Text("Samples / Axis  %u", terrain.settings.samplesPerAxis);
			ImGui::Text("Voxel Size      %.2f m", terrain.settings.voxelSizeMeter);
		}
	}
	ImGui::End();

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
void TerrainApp::ConfigureRenderGraph()
{
	auto& device = *m_pRendererBackend->GetDevice();

	m_pScene->AddRenderNode(MakeArc< StaticSkyboxNode >(device));
	{
		auto pGBufferNode = MakeArc< GBufferNode >(device);
		auto pCullingNode = MakeArc< CullingNode >(device);
		auto pVoxelNode   = MakeArc< VoxelChunkRenderNode >(device);

		pCullingNode->SetGBufferNode(pGBufferNode);
		pCullingNode->SetVoxelNode(pVoxelNode);

		m_pScene->AddRenderNode(pCullingNode);
	}
	m_pScene->AddRenderNode(MakeArc< SurfaceResolveNode >(device));
	m_pScene->AddRenderNode(MakeArc< LightingNode >(device));
	m_pScene->AddRenderNode(MakeArc< DebugDrawNode >(device));
	m_pScene->AddRenderNode(MakeArc< PostProcessNode >(device));
}

void TerrainApp::ConfigureSceneObjects()
{
	m_VoxelTerrainRootEntity = m_pScene->CreateEntity(kVoxelTerrainRootTag);
	auto& terrain = m_VoxelTerrainRootEntity.AttachComponent< VoxelTerrainComponent >();
	{
		auto& transform = m_VoxelTerrainRootEntity.GetComponent< TransformComponent >();
		transform.transform.position = terrain.terrainOriginWorld;
		transform.transform.rotation = float3(0.0f);
		transform.transform.scale = float3(1.0f);
		transform.transform.Update();
		m_pScene->Registry().patch< TransformComponent >(m_VoxelTerrainRootEntity.ID(), [](auto&) {});
	}

	{
		auto skybox = m_pScene->CreateEntity("Skybox");
		auto& transform = skybox.GetComponent< TransformComponent >();
		transform.transform.position = float3(0.0f, 1.0f, 0.0f);

		auto& light = skybox.AttachComponent< LightComponent >();
		light.SetDefaultDirectionalLight();

		auto& atmosphere = skybox.AttachComponent< AtmosphereComponent >();
		atmosphere.skybox = TEXTURE_PATH.string() + "Skybox_Field.jpg";

	}

	{
		auto  volume = m_pScene->CreateEntity("PostProcessVolume");
		auto& pp = volume.AttachComponent< PostProcessComponent >();

		pp.tonemap.op    = eToneMappingOp::ACES;
		pp.tonemap.ev100 = 0.0f;
		pp.tonemap.gamma = 2.2f;
	}
}
