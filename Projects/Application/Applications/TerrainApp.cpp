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
#include "BaambooScene/VoxelTerrain/VoxelTerrainFieldProfiles.h"

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

	m_CameraController.SetLookAt(float3(96.0f, 64.0f, -96.0f), float3(32.0f, 32.0f, 32.0f));
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

			if (ImGui::BeginCombo("Field Preset", GetVoxelTerrainFieldPresetName(terrain.fieldPreset)))
			{
				for (u32 i = 0u; i < GetVoxelTerrainFieldPresetCount(); ++i)
				{
					const VoxelTerrainFieldPreset preset = GetVoxelTerrainFieldPresetAt(i);
					const bool bSelected = terrain.fieldPreset == preset;
					if (ImGui::Selectable(GetVoxelTerrainFieldPresetName(preset), bSelected))
					{
						terrain.fieldPreset = preset;
						bRebuildChunk = true;
					}

					if (bSelected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}

			ImGui::Text("Built Preset    %s", GetVoxelTerrainFieldPresetName(terrain.builtFieldPreset));
			ImGui::DragFloat3("Terrain Origin", glm::value_ptr(terrain.terrainOriginWorld), 0.5f, 0.0f, 0.0f, "%.2f m");
			if (ImGui::IsItemDeactivatedAfterEdit())
				bRebuildChunk = true;

			switch (terrain.fieldPreset)
			{
			case VoxelTerrainFieldPreset::AxisAlignedBox:
				ImGui::Text("Box Center     %.1f, %.1f, %.1f", terrain.boxCenter.x, terrain.boxCenter.y, terrain.boxCenter.z);
				ImGui::Text("Box Half Ext   %.1f, %.1f, %.1f", terrain.boxHalfExtent.x, terrain.boxHalfExtent.y, terrain.boxHalfExtent.z);
				break;
			case VoxelTerrainFieldPreset::Capsule:
				ImGui::Text("Capsule A      %.1f, %.1f, %.1f", terrain.capsuleSegmentA.x, terrain.capsuleSegmentA.y, terrain.capsuleSegmentA.z);
				ImGui::Text("Capsule B      %.1f, %.1f, %.1f", terrain.capsuleSegmentB.x, terrain.capsuleSegmentB.y, terrain.capsuleSegmentB.z);
				ImGui::Text("Capsule Radius %.1f m", terrain.capsuleRadius);
				break;
			case VoxelTerrainFieldPreset::UniformTransformedBox:
				ImGui::Text("Box Center     %.1f, %.1f, %.1f", terrain.transformBoxCenter.x, terrain.transformBoxCenter.y, terrain.transformBoxCenter.z);
				ImGui::Text("Box Half Ext   %.1f, %.1f, %.1f", terrain.transformBoxHalfExtent.x, terrain.transformBoxHalfExtent.y, terrain.transformBoxHalfExtent.z);
				ImGui::Text("Rotation XYZ   %.1f, %.1f, %.1f deg", terrain.transformBoxEulerDegrees.x, terrain.transformBoxEulerDegrees.y, terrain.transformBoxEulerDegrees.z);
				ImGui::Text("Uniform Scale  %.2f", terrain.transformUniformScale);
				break;
			case VoxelTerrainFieldPreset::NonUniformDistanceLikeBox:
				ImGui::Text("Box Center     %.1f, %.1f, %.1f", terrain.transformBoxCenter.x, terrain.transformBoxCenter.y, terrain.transformBoxCenter.z);
				ImGui::Text("Box Half Ext   %.1f, %.1f, %.1f", terrain.transformBoxHalfExtent.x, terrain.transformBoxHalfExtent.y, terrain.transformBoxHalfExtent.z);
				ImGui::Text("Rotation XYZ   %.1f, %.1f, %.1f deg", terrain.transformBoxEulerDegrees.x, terrain.transformBoxEulerDegrees.y, terrain.transformBoxEulerDegrees.z);
				ImGui::Text("NonUni Scale   %.2f, %.2f, %.2f", terrain.transformNonUniformScale.x, terrain.transformNonUniformScale.y, terrain.transformNonUniformScale.z);
				break;
			case VoxelTerrainFieldPreset::HeightFieldFlat:
			case VoxelTerrainFieldPreset::HeightFieldSloped:
			case VoxelTerrainFieldPreset::HeightFieldPeriodic:
				if (const VoxelTerrainHeightFieldParameters* params = GetVoxelTerrainHeightFieldParameters(terrain.fieldPreset))
				{
					ImGui::Text("Height Shape   %s", GetVoxelTerrainHeightFieldShapeName(params->shape));
					ImGui::Text("Base Height    %.2f m", params->baseHeightMeter);
					ImGui::Text("Anchor XZ      %.2f, %.2f m", params->anchorXMeter, params->anchorZMeter);
					if (params->shape == VoxelTerrainHeightFieldShape::Plane)
						ImGui::Text("Slope XZ       %.3f, %.3f", params->slopeX, params->slopeZ);
					if (params->shape == VoxelTerrainHeightFieldShape::Periodic)
					{
						ImGui::Text("Amplitude      %.2f m", params->amplitudeMeter);
						ImGui::Text("Wavelength XZ  %.2f, %.2f m", params->wavelengthXMeter, params->wavelengthZMeter);
					}
				}
				break;
			case VoxelTerrainFieldPreset::SphereRegression:
				break;
			}

			ImGui::DragFloat("FD Epsilon Mult", &terrain.settings.normalEpsilonMultiplier, 0.01f, 0.01f, 4.0f, "%.2f");
			if (ImGui::IsItemDeactivatedAfterEdit())
				bRebuildChunk = true;

			terrain.settings.normalEpsilonMultiplier = std::max(terrain.settings.normalEpsilonMultiplier, 0.01f);

			if (ImGui::Button("Rebuild CPU Chunk"))
				bRebuildChunk = true;

			if (bRebuildChunk)
				m_pScene->Registry().patch< VoxelTerrainComponent >(m_VoxelTerrainRootEntity.ID(), [](auto&) {});

			ImGui::Separator();
			ImGui::Text("Chunk Size      %.1f m", terrain.settings.chunkWorldSizeMeter);
			ImGui::Text("Cells / Axis    %u", terrain.settings.cellsPerAxis);
			ImGui::Text("Samples / Axis  %u", terrain.settings.samplesPerAxis);
			ImGui::Text("Voxel Size      %.2f m", terrain.settings.voxelSizeMeter);
			ImGui::Text("FD Epsilon      %.2f x voxel", terrain.settings.normalEpsilonMultiplier);
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
