#include "TerrainApp.h"

#include "BaambooCore/Common.h"
#include "BaambooCore/Window.h"
#include "BaambooCore/Input.hpp"

#include "BaambooScene/Entity.h"
#include "BaambooScene/Components.h"
#include "BaambooScene/RenderNodes/GBufferNode.h"
#include "BaambooScene/RenderNodes/CullingNode.h"
#include "BaambooScene/RenderNodes/SkyboxNode.h"
#include "BaambooScene/RenderNodes/LightingNode.h"
#include "BaambooScene/RenderNodes/SurfaceResolveNode.h"
#include "BaambooScene/RenderNodes/DebugDrawNode.h"
#include "BaambooScene/RenderNodes/PostProcessNode.h"
#include "BaambooScene/Systems/TransformSystem.h"
#include "BaambooScene/Systems/VoxelTerrainSystem.h"
#include "BaambooScene/VoxelTerrain/VoxelTerrainFieldProfiles.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <unordered_set>

#include <glm/gtc/type_ptr.hpp>
#include <imgui/backends/imgui_impl_glfw.h>

using namespace baamboo;

namespace
{

constexpr const char* kVoxelTerrainRootTag = "VoxelTerrainRoot";
constexpr const char* kVoxelTerrainChunkTag = "VoxelTerrainChunk";
constexpr const char* kVoxelTerrainGeneratedPath = "$generated/VoxelTerrainChunk";

bool IsFinite(const float3& v)
{
	return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

float3 TransformPoint(const mat4& transform, const float3& point)
{
	const float4 transformed = transform * float4(point, 1.0f);
	return float3(transformed.x, transformed.y, transformed.z);
}

float3 TransformNormal(const mat4& transform, const float3& normal)
{
	const mat3 normalMatrix = glm::transpose(glm::inverse(mat3(transform)));
	return normalMatrix * normal;
}

u64 MakeDebugEdgeKey(u32 a, u32 b)
{
	const u32 lo = std::min(a, b);
	const u32 hi = std::max(a, b);
	return (static_cast< u64 >(lo) << 32u) | static_cast< u64 >(hi);
}

void AddDebugLine(
	std::vector< DebugLineVertex >& lines,
	const float3& a,
	const float3& b,
	const float3& color,
	float alpha = 1.0f)
{
	DebugLineVertex va = {};
	va.position = a;
	va.color = color;
	va.alpha = alpha;

	DebugLineVertex vb = va;
	vb.position = b;

	lines.push_back(va);
	lines.push_back(vb);
}

void AddTransformedAabbLines(std::vector< DebugLineVertex >& lines, const BoundingBox& aabb, const mat4& localToWorld, const float3& color, float alpha)
{
	const float3 min = aabb.Min();
	const float3 max = aabb.Max();
	const float3 corners[8] = {
		TransformPoint(localToWorld, { min.x, min.y, min.z }),
		TransformPoint(localToWorld, { max.x, min.y, min.z }),
		TransformPoint(localToWorld, { max.x, max.y, min.z }),
		TransformPoint(localToWorld, { min.x, max.y, min.z }),
		TransformPoint(localToWorld, { min.x, min.y, max.z }),
		TransformPoint(localToWorld, { max.x, min.y, max.z }),
		TransformPoint(localToWorld, { max.x, max.y, max.z }),
		TransformPoint(localToWorld, { min.x, max.y, max.z }),
	};

	const u32 edges[24] = {
		0u, 1u, 1u, 2u, 2u, 3u, 3u, 0u,
		4u, 5u, 5u, 6u, 6u, 7u, 7u, 4u,
		0u, 4u, 1u, 5u, 2u, 6u, 3u, 7u,
	};

	for (u32 i = 0u; i < 24u; i += 2u)
		AddDebugLine(lines, corners[edges[i]], corners[edges[i + 1u]], color, alpha);
}

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
	if (m_bVoxelWireframeVisible || m_bVoxelChunkBoundsVisible || m_bVoxelNormalsVisible)
		RefreshVoxelTerrainDebugLines();
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
			bool bRefreshMesh = false;
			bool bRefreshDebugLines = false;
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

			bRefreshMesh |= ImGui::Checkbox("Shaded Mesh", &m_bVoxelMeshVisible);
			bRefreshDebugLines |= ImGui::Checkbox("Wireframe Overlay", &m_bVoxelWireframeVisible);
			bRefreshDebugLines |= ImGui::Checkbox("Chunk AABB", &m_bVoxelChunkBoundsVisible);
			bRefreshDebugLines |= ImGui::Checkbox("SDF Normal Lines", &m_bVoxelNormalsVisible);
			bRefreshDebugLines |= ImGui::DragFloat("Normal Line Length", &m_VoxelNormalLineLength, 0.05f, 0.01f, 10.0f, "%.2f m");
			bRefreshDebugLines |= ImGui::InputInt("Normal Stride", &m_VoxelNormalStride);
			bRefreshDebugLines |= ImGui::InputInt("Normal Max Count", &m_VoxelNormalMaxCount);
			ImGui::DragFloat("FD Epsilon Mult", &terrain.settings.normalEpsilonMultiplier, 0.01f, 0.01f, 4.0f, "%.2f");
			if (ImGui::IsItemDeactivatedAfterEdit())
				bRebuildChunk = true;

			m_VoxelNormalStride = std::max(m_VoxelNormalStride, 1);
			m_VoxelNormalMaxCount = std::max(m_VoxelNormalMaxCount, 0);
			m_VoxelNormalLineLength = std::max(m_VoxelNormalLineLength, 0.01f);
			terrain.settings.normalEpsilonMultiplier = std::max(terrain.settings.normalEpsilonMultiplier, 0.01f);

			if (ImGui::Button("Rebuild CPU Chunk"))
				bRebuildChunk = true;
			ImGui::SameLine();
			if (ImGui::Button("Refresh Stats"))
				RefreshVoxelTerrainStats();

			if (bRebuildChunk)
			{
				RebuildVoxelTerrain(true);
				bRefreshMesh = false;
				bRefreshDebugLines = false;
			}
			if (bRefreshMesh)
			{
				if (VoxelTerrainSystem* voxelSystem = m_pScene ? m_pScene->GetVoxelTerrainSystem() : nullptr)
					voxelSystem->SetMeshVisible(m_bVoxelMeshVisible);
			}
			if (bRefreshDebugLines || bRefreshMesh)
				RefreshVoxelTerrainDebugLines(true);

			ImGui::Separator();
			ImGui::Text("Chunk Size      %.1f m", terrain.settings.chunkWorldSizeMeter);
			ImGui::Text("Cells / Axis    %u", terrain.settings.cellsPerAxis);
			ImGui::Text("Samples / Axis  %u", terrain.settings.samplesPerAxis);
			ImGui::Text("Voxel Size      %.2f m", terrain.settings.voxelSizeMeter);
			ImGui::Text("FD Epsilon      %.2f x voxel", terrain.settings.normalEpsilonMultiplier);

			ImGui::Separator();
			ImGui::Text("Chunks          %u", m_VoxelTerrainStats.numChunks);
			ImGui::Text("Allocated       %u", m_VoxelTerrainStats.numAllocatedSamples);
			ImGui::Text("Valid / Invalid %u / %u", m_VoxelTerrainStats.numValidSamples, m_VoxelTerrainStats.numInvalidSamples);
			ImGui::Text("Solid / Air     %u / %u", m_VoxelTerrainStats.numSolidSamples, m_VoxelTerrainStats.numAirSamples);
			ImGui::Text("Surface Samples %u", m_VoxelTerrainStats.numSurfaceSamples);
			ImGui::Text("SDF Min / Max   %.3f / %.3f", m_VoxelTerrainStats.minSDF, m_VoxelTerrainStats.maxSDF);

			ImGui::Separator();
			ImGui::Text("Surface Cells   %u", m_VoxelTerrainStats.numSurfaceCells);
			ImGui::Text("Cube Indices    %u active", m_VoxelTerrainStats.numActiveCubeIndices);
			ImGui::Text("Mesh Vertices   %u", m_VoxelTerrainStats.numMeshVertices);
			ImGui::Text("Mesh Indices    %u", m_VoxelTerrainStats.numMeshIndices);
			ImGui::Text("Meshlets        %u", m_VoxelTerrainStats.numMeshlets);
			ImGui::Text("Normal Fallback %u", m_VoxelTerrainStats.numNormalGradientFallbacks);

			ImGui::Separator();
			ImGui::Text("Mesh Bounds     %u valid", m_VoxelTerrainStats.numMeshesWithBounds);
			ImGui::Text("Bounds Min      %.3f, %.3f, %.3f", m_VoxelTerrainStats.meshBoundsMin.x, m_VoxelTerrainStats.meshBoundsMin.y, m_VoxelTerrainStats.meshBoundsMin.z);
			ImGui::Text("Bounds Max      %.3f, %.3f, %.3f", m_VoxelTerrainStats.meshBoundsMax.x, m_VoxelTerrainStats.meshBoundsMax.y, m_VoxelTerrainStats.meshBoundsMax.z);

			ImGui::Separator();
			ImGui::Text("Normals         %u sampled", m_VoxelTerrainStats.numNormalVertices);
			ImGui::Text("Normal Len      %.4f / %.4f / %.4f", m_VoxelTerrainStats.minNormalLength, m_VoxelTerrainStats.avgNormalLength, m_VoxelTerrainStats.maxNormalLength);
			ImGui::Text("Avg Normal      %.3f, %.3f, %.3f", m_VoxelTerrainStats.avgNormal.x, m_VoxelTerrainStats.avgNormal.y, m_VoxelTerrainStats.avgNormal.z);
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

		pCullingNode->SetGBufferNode(pGBufferNode);

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

	m_VoxelTerrainChunkEntity = m_pScene->CreateEntity(kVoxelTerrainChunkTag);
	m_VoxelTerrainRootEntity.AttachChild(m_VoxelTerrainChunkEntity.ID());
	{
		auto& chunkComponent = m_VoxelTerrainChunkEntity.AttachComponent< VoxelTerrainChunkComponent >();
		chunkComponent.root = m_VoxelTerrainRootEntity.ID();

		auto& mesh = m_VoxelTerrainChunkEntity.AttachComponent< StaticMeshComponent >();
		mesh.tag = kVoxelTerrainChunkTag;
		mesh.path = kVoxelTerrainGeneratedPath;
		mesh.maxLOD = 0u;

		auto& material = m_VoxelTerrainChunkEntity.AttachComponent< MaterialComponent >();
		material.name = "Voxel Terrain Reference";
		material.tint = float4(0.74f, 0.82f, 0.92f, 1.0f);
		material.roughness = 0.72f;
		material.metallic = 0.0f;
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

	RebuildVoxelTerrain();
}

void TerrainApp::RebuildVoxelTerrain(bool bSceneAlreadyLocked)
{
	if (!m_pScene || !m_VoxelTerrainRootEntity.IsValid())
		return;

	VoxelTerrainSystem* voxelSystem = m_pScene->GetVoxelTerrainSystem();
	if (!voxelSystem)
		return;

	voxelSystem->Rebuild(m_VoxelTerrainRootEntity.ID());
	RefreshVoxelTerrainStats();
	RefreshVoxelTerrainDebugLines(bSceneAlreadyLocked);
}
void TerrainApp::RefreshVoxelTerrainStats()
{
	VoxelTerrainSystem* voxelSystem = m_pScene ? m_pScene->GetVoxelTerrainSystem() : nullptr;
	const ProceduralTerrain* terrainData = (voxelSystem && m_VoxelTerrainRootEntity.IsValid())
		? voxelSystem->GetTerrain(m_VoxelTerrainRootEntity.ID())
		: nullptr;

	if (!terrainData)
	{
		ProceduralTerrain emptyTerrain;
		m_VoxelTerrainStats = VoxelTerrainDebug::CollectStats(emptyTerrain);
		return;
	}

	m_VoxelTerrainStats = VoxelTerrainDebug::CollectStats(*terrainData);
}void TerrainApp::RefreshVoxelTerrainDebugLines(bool bSceneAlreadyLocked)
{
	if (!m_pScene)
		return;

	auto ClearDebugLines = [&]()
		{
			if (bSceneAlreadyLocked)
				m_pScene->ClearDebugLinesAlreadyLocked();
			else
				m_pScene->ClearDebugLines();
		};

	if (!m_VoxelTerrainRootEntity.IsValid() ||
		!m_VoxelTerrainChunkEntity.IsValid() ||
		!m_VoxelTerrainRootEntity.HasAll< VoxelTerrainComponent, TransformComponent >() ||
		!m_VoxelTerrainChunkEntity.HasAll< VoxelTerrainChunkComponent, TransformComponent >())
	{
		ClearDebugLines();
		return;
	}

	VoxelTerrainSystem* voxelSystem = m_pScene->GetVoxelTerrainSystem();
	if (!voxelSystem)
	{
		ClearDebugLines();
		return;
	}

	const auto& terrain = m_VoxelTerrainRootEntity.GetComponent< VoxelTerrainComponent >();
	const SDFChunk* chunk = voxelSystem->GetChunk(m_VoxelTerrainChunkEntity.ID());
	if (!chunk)
	{
		ClearDebugLines();
		return;
	}

	voxelSystem->NormalizeRootTransform(m_VoxelTerrainRootEntity.ID());
	voxelSystem->NormalizeChunkTransform(m_VoxelTerrainChunkEntity.ID());

	const auto& rootTransform = m_VoxelTerrainRootEntity.GetComponent< TransformComponent >();
	const auto& chunkTransform = m_VoxelTerrainChunkEntity.GetComponent< TransformComponent >();
	const mat4 chunkToWorld = rootTransform.transform.Matrix() * chunkTransform.transform.Matrix();

	std::vector< DebugLineVertex > lines;
	const TerrainMeshData& meshData = chunk->MeshData();

	if (m_bVoxelChunkBoundsVisible)
	{
		const float chunkSize = terrain.settings.chunkWorldSizeMeter;
		const BoundingBox chunkBounds(float3(0.0f), float3(chunkSize));
		AddTransformedAabbLines(lines, chunkBounds, chunkToWorld, float3(1.0f, 0.84f, 0.22f), 1.0f);
	}

	if (m_bVoxelWireframeVisible)
	{
		std::unordered_set< u64 > edges;
		for (u32 index = 0u; index + 2u < meshData.NumIndices(); index += 3u)
		{
			const u32 i0 = meshData.indices[index + 0u];
			const u32 i1 = meshData.indices[index + 1u];
			const u32 i2 = meshData.indices[index + 2u];
			if (i0 >= meshData.NumVertices() || i1 >= meshData.NumVertices() || i2 >= meshData.NumVertices())
				continue;

			const u32 edgeIndices[6] = { i0, i1, i1, i2, i2, i0 };
			for (u32 e = 0u; e < 6u; e += 2u)
			{
				const u32 a = edgeIndices[e];
				const u32 b = edgeIndices[e + 1u];
				if (!edges.insert(MakeDebugEdgeKey(a, b)).second)
					continue;

				AddDebugLine(
					lines,
					TransformPoint(chunkToWorld, meshData.vertices[a].position),
					TransformPoint(chunkToWorld, meshData.vertices[b].position),
					float3(0.08f, 0.92f, 1.0f),
					0.65f);
			}
		}
	}

	if (m_bVoxelNormalsVisible && m_VoxelNormalMaxCount > 0)
	{
		const u32 stride = static_cast< u32 >(std::max(m_VoxelNormalStride, 1));
		const u32 maxCount = static_cast< u32 >(m_VoxelNormalMaxCount);
		const u32 numVertices = meshData.NumVertices();
		const u32 candidateCount = (numVertices + stride - 1u) / stride;
		const u32 displayCount = std::min(maxCount, candidateCount);

		for (u32 displayed = 0u; displayed < displayCount; ++displayed)
		{
			u32 candidateIndex = displayed;
			if (candidateCount > displayCount)
			{
				candidateIndex = (displayCount > 1u)
					? (displayed * (candidateCount - 1u)) / (displayCount - 1u)
					: candidateCount / 2u;
			}

			const u32 vertexIndex = std::min(candidateIndex * stride, numVertices - 1u);
			const Vertex& vertex = meshData.vertices[vertexIndex];
			if (!IsFinite(vertex.normal))
				continue;

			const float3 normal = TransformNormal(chunkToWorld, vertex.normal);
			const float normalLength = glm::length(normal);
			if (!IsFinite(normal) || !std::isfinite(normalLength) || normalLength <= 1e-6f)
				continue;

			const float3 start = TransformPoint(chunkToWorld, vertex.position);
			const float3 end = start + (normal / normalLength) * m_VoxelNormalLineLength;
			AddDebugLine(lines, start, end, float3(0.25f, 1.0f, 0.35f), 1.0f);
		}
	}

	if (bSceneAlreadyLocked)
		m_pScene->SetDebugLinesAlreadyLocked(std::move(lines));
	else
		m_pScene->SetDebugLines(std::move(lines));
}
