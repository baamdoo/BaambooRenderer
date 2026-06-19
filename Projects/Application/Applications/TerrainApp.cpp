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

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <unordered_set>

#include <glm/gtc/type_ptr.hpp>
#include <imgui/backends/imgui_impl_glfw.h>

using namespace baamboo;

namespace
{

constexpr const char* kVoxelTerrainMeshTag = "VoxelSphereChunk";
constexpr const char* kVoxelTerrainGeneratedPath = "$generated/VoxelSphereChunk";

bool IsFinite(const float3& v)
{
	return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
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

void AddAabbLines(std::vector< DebugLineVertex >& lines, const BoundingBox& aabb, const float3& color, float alpha)
{
	const float3 min = aabb.Min();
	const float3 max = aabb.Max();
	const float3 corners[8] = {
		{ min.x, min.y, min.z },
		{ max.x, min.y, min.z },
		{ max.x, max.y, min.z },
		{ min.x, max.y, min.z },
		{ min.x, min.y, max.z },
		{ max.x, min.y, max.z },
		{ max.x, max.y, max.z },
		{ min.x, max.y, max.z },
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
		if (ImGui::CollapsingHeader("CPU Marching Cubes Validation", ImGuiTreeNodeFlags_DefaultOpen))
		{
			bool bRefreshMesh = false;
			bool bRefreshDebugLines = false;
			bool bRebuildChunk = false;

			bRefreshMesh |= ImGui::Checkbox("Shaded Mesh", &m_bVoxelMeshVisible);
			bRefreshDebugLines |= ImGui::Checkbox("Wireframe Overlay", &m_bVoxelWireframeVisible);
			bRefreshDebugLines |= ImGui::Checkbox("Chunk AABB", &m_bVoxelChunkBoundsVisible);
			bRefreshDebugLines |= ImGui::Checkbox("SDF Normal Lines", &m_bVoxelNormalsVisible);
			bRefreshDebugLines |= ImGui::DragFloat("Normal Line Length", &m_VoxelNormalLineLength, 0.05f, 0.01f, 10.0f, "%.2f m");
			bRefreshDebugLines |= ImGui::InputInt("Normal Stride", &m_VoxelNormalStride);
			bRefreshDebugLines |= ImGui::InputInt("Normal Max Count", &m_VoxelNormalMaxCount);
			ImGui::DragFloat("FD Epsilon Mult", &m_VoxelTerrainSettings.normalEpsilonMultiplier, 0.01f, 0.01f, 4.0f, "%.2f");
			if (ImGui::IsItemDeactivatedAfterEdit())
				bRebuildChunk = true;

			m_VoxelNormalStride = std::max(m_VoxelNormalStride, 1);
			m_VoxelNormalMaxCount = std::max(m_VoxelNormalMaxCount, 0);
			m_VoxelNormalLineLength = std::max(m_VoxelNormalLineLength, 0.01f);
			m_VoxelTerrainSettings.normalEpsilonMultiplier = std::max(m_VoxelTerrainSettings.normalEpsilonMultiplier, 0.01f);

			if (ImGui::Button("Rebuild CPU Chunk"))
				bRebuildChunk = true;

			if (bRebuildChunk)
			{
				RebuildVoxelTerrain(true);
				bRefreshMesh = false;
				bRefreshDebugLines = false;
			}
			if (ImGui::Button("Refresh Validation Stats"))
				m_VoxelTerrainStats = VoxelTerrainDebug::CollectStats(m_VoxelTerrain);

			if (bRefreshMesh)
				RefreshVoxelTerrainMeshComponent();
			if (bRefreshDebugLines || bRefreshMesh)
				RefreshVoxelTerrainDebugLines(true);
		}

		ImGui::Separator();
		ImGui::Text("Chunk Size      %.1f m", m_VoxelTerrainSettings.chunkWorldSizeMeter);
		ImGui::Text("Cells / Axis    %u", m_VoxelTerrainSettings.cellsPerAxis);
		ImGui::Text("Samples / Axis  %u", m_VoxelTerrainSettings.samplesPerAxis);
		ImGui::Text("Voxel Size      %.2f m", m_VoxelTerrainSettings.voxelSizeMeter);
		ImGui::Text("FD Epsilon      %.2f x voxel", m_VoxelTerrainSettings.normalEpsilonMultiplier);

		ImGui::Separator();
		ImGui::Text("Chunks          %u", m_VoxelTerrainStats.numChunks);
		ImGui::Text("Allocated       %u", m_VoxelTerrainStats.numAllocatedSamples);
		ImGui::Text("Valid           %u", m_VoxelTerrainStats.numValidSamples);
		ImGui::Text("Invalid         %u", m_VoxelTerrainStats.numInvalidSamples);
		ImGui::Text("Solid           %u", m_VoxelTerrainStats.numSolidSamples);
		ImGui::Text("Air             %u", m_VoxelTerrainStats.numAirSamples);
		ImGui::Text("Surface         %u", m_VoxelTerrainStats.numSurfaceSamples);
		ImGui::Text("SDF Min / Max   %.3f / %.3f", m_VoxelTerrainStats.minSDF, m_VoxelTerrainStats.maxSDF);

		ImGui::Separator();
		ImGui::Text("Surface Cells   %u", m_VoxelTerrainStats.numSurfaceCells);
		ImGui::Text("Cube Indices    %u active", m_VoxelTerrainStats.numActiveCubeIndices);
		ImGui::Text("Mesh Vertices   %u", m_VoxelTerrainStats.numMeshVertices);
		ImGui::Text("Mesh Indices    %u", m_VoxelTerrainStats.numMeshIndices);

		ImGui::Separator();
		ImGui::Text("Mesh Bounds     %u valid", m_VoxelTerrainStats.numMeshesWithBounds);
		ImGui::Text("Bounds Min      %.3f, %.3f, %.3f",
			m_VoxelTerrainStats.meshBoundsMin.x,
			m_VoxelTerrainStats.meshBoundsMin.y,
			m_VoxelTerrainStats.meshBoundsMin.z);
		ImGui::Text("Bounds Max      %.3f, %.3f, %.3f",
			m_VoxelTerrainStats.meshBoundsMax.x,
			m_VoxelTerrainStats.meshBoundsMax.y,
			m_VoxelTerrainStats.meshBoundsMax.z);

		ImGui::Separator();
		ImGui::Text("Residuals       %u valid, %u non-finite",
			m_VoxelTerrainStats.numResidualVertices,
			m_VoxelTerrainStats.numNonFiniteResiduals);
		ImGui::Text("Abs SDF         %.6f / %.6f / %.6f",
			m_VoxelTerrainStats.minSurfaceResidual,
			m_VoxelTerrainStats.avgSurfaceResidual,
			m_VoxelTerrainStats.maxSurfaceResidual);

		ImGui::Separator();
		ImGui::Text("Normals         %u valid", m_VoxelTerrainStats.numNormalVertices);
		ImGui::Text("Normal Issues   %u zero, %u non-finite", m_VoxelTerrainStats.numZeroNormals, m_VoxelTerrainStats.numNonFiniteNormals);
		ImGui::Text("Normal Len      %.4f / %.4f / %.4f",
			m_VoxelTerrainStats.minNormalLength,
			m_VoxelTerrainStats.avgNormalLength,
			m_VoxelTerrainStats.maxNormalLength);
		ImGui::Text("Avg Normal      %.3f, %.3f, %.3f",
			m_VoxelTerrainStats.avgNormal.x,
			m_VoxelTerrainStats.avgNormal.y,
			m_VoxelTerrainStats.avgNormal.z);
		ImGui::Text("Sphere Dot      %.4f / %.4f / %.4f",
			m_VoxelTerrainStats.minSphereNormalDot,
			m_VoxelTerrainStats.avgSphereNormalDot,
			m_VoxelTerrainStats.maxSphereNormalDot);
		ImGui::Text("Sphere Normals  %u outward, %u inward",
			m_VoxelTerrainStats.numSphereOutwardNormals,
			m_VoxelTerrainStats.numSphereInwardNormals);

		ImGui::Separator();
		ImGui::Text("Triangles       %u", m_VoxelTerrainStats.numTriangles);
		ImGui::Text("Index/DeGen     %u invalid, %u degenerate",
			m_VoxelTerrainStats.numInvalidIndexTriangles,
			m_VoxelTerrainStats.numDegenerateTriangles);
		ImGui::Text("Face Dot        %.4f / %.4f / %.4f",
			m_VoxelTerrainStats.minFaceNormalDot,
			m_VoxelTerrainStats.avgFaceNormalDot,
			m_VoxelTerrainStats.maxFaceNormalDot);
		ImGui::Text("Winding         %u negative-dot tris",
			m_VoxelTerrainStats.numNegativeFaceNormalDotTriangles);
		ImGui::Text("Edges           %u boundary, %u non-manifold",
			m_VoxelTerrainStats.numBoundaryEdges,
			m_VoxelTerrainStats.numNonManifoldEdges);

		if (ImGui::CollapsingHeader("Cube Index Histogram"))
		{
			float allHistogram[256] = {};
			u32 maxAllCount = 0u;
			u32 maxSurfacePatternCount = 0u;
			u32 surfacePatternCellCount = 0u;

			for (u32 cubeIndex = 0u; cubeIndex < 256u; ++cubeIndex)
			{
				const u32 count = m_VoxelTerrainStats.cubeIndexHistogram[cubeIndex];
				allHistogram[cubeIndex] = static_cast<float>(count);

				if (maxAllCount < count)
					maxAllCount = count;

				if (cubeIndex != 0u && cubeIndex != 255u)
				{
					surfacePatternCellCount += count;
					if (maxSurfacePatternCount < count)
						maxSurfacePatternCount = count;
				}
			}

			const float histogramWidth = ImGui::GetContentRegionAvail().x;
			ImGui::Text("All Cells       max %u", maxAllCount);
			ImGui::PlotHistogram(
				"##cube_index_histogram_all",
				allHistogram,
				256,
				0,
				nullptr,
				0.0f,
				static_cast<float>(maxAllCount > 0u ? maxAllCount : 1u),
				ImVec2(histogramWidth, 120.0f));
			ImGui::Text("Air 0x00        %u", m_VoxelTerrainStats.cubeIndexHistogram[0u]);
			ImGui::Text("Solid 0xFF      %u", m_VoxelTerrainStats.cubeIndexHistogram[255u]);

			ImGui::Separator();
			ImGui::Text("Surface Patterns %u cells, max %u", surfacePatternCellCount, maxSurfacePatternCount);

			if (maxSurfacePatternCount > 0u)
			{
				ImGui::BeginChild("##cube_index_surface_pattern_bars", ImVec2(0.0f, 220.0f), true);
				for (u32 cubeIndex = 1u; cubeIndex < 255u; ++cubeIndex)
				{
					const u32 count = m_VoxelTerrainStats.cubeIndexHistogram[cubeIndex];
					if (count == 0u)
						continue;

					char overlay[32] = {};
					std::snprintf(overlay, sizeof(overlay), "%u", count);

					ImGui::Text("0x%02X", cubeIndex);
					ImGui::SameLine(56.0f);
					ImGui::ProgressBar(
						static_cast<float>(count) / static_cast<float>(maxSurfacePatternCount),
						ImVec2(-1.0f, 0.0f),
						overlay);
				}
				ImGui::EndChild();
			}
			else
			{
				ImGui::TextDisabled("No surface-pattern cube indices.");
			}
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
	m_VoxelTerrainEntity = m_pScene->CreateEntity(kVoxelTerrainMeshTag);
	{
		auto& mesh = m_VoxelTerrainEntity.AttachComponent< StaticMeshComponent >();
		mesh.tag = kVoxelTerrainMeshTag;
		mesh.path = kVoxelTerrainGeneratedPath;
		mesh.maxLOD = 0u;

		auto& material = m_VoxelTerrainEntity.AttachComponent< MaterialComponent >();
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
	m_VoxelTerrain.Clear();
	m_VoxelTerrain.Initialize(m_VoxelTerrainSettings);

	m_VoxelChunkIndex = m_VoxelTerrain.CreateChunk(float3(0.0f, 0.0f, 0.0f));
	m_VoxelTerrain.BuildChunkSamples(m_VoxelChunkIndex);
	m_VoxelTerrain.BuildChunkMesh(m_VoxelChunkIndex);

	m_VoxelTerrainStats = VoxelTerrainDebug::CollectStats(m_VoxelTerrain);
	RefreshVoxelTerrainMeshComponent();
	RefreshVoxelTerrainDebugLines(bSceneAlreadyLocked);
}

void TerrainApp::RefreshVoxelTerrainMeshComponent()
{
	if (!m_pScene || !m_VoxelTerrainEntity.IsValid() || !m_VoxelTerrainEntity.HasAll< StaticMeshComponent >())
		return;

	auto& meshComponent = m_VoxelTerrainEntity.GetComponent< StaticMeshComponent >();
	meshComponent.tag = kVoxelTerrainMeshTag;
	meshComponent.path = kVoxelTerrainGeneratedPath;
	meshComponent.pVertices = nullptr;
	meshComponent.numVertices = 0u;
	meshComponent.aabb = BoundingBox(float3(0.0f), float3(0.0f));
	meshComponent.sphere = BoundingSphere(float3(0.0f), 0.0f);
	meshComponent.maxLOD = 0u;
	for (auto& lod : meshComponent.lods)
	{
		lod.pIndices = nullptr;
		lod.numIndices = 0u;
		lod.pMeshlets = nullptr;
		lod.numMeshlets = 0u;
		lod.pMeshletVertices = nullptr;
		lod.numMeshletVertices = 0u;
		lod.pMeshletTriangles = nullptr;
		lod.numMeshletTriangles = 0u;
		lod.simplifyError = 0.0f;
	}

	const SDFChunk* chunk = m_VoxelTerrain.GetChunk(m_VoxelChunkIndex);
	if (chunk)
	{
		auto& transform = m_VoxelTerrainEntity.GetComponent< TransformComponent >();
		transform.transform.position = chunk->GetOriginWorld();
		m_pScene->Registry().patch< TransformComponent >(m_VoxelTerrainEntity.ID(), [](auto&) {});
	}

	if (chunk && m_bVoxelMeshVisible)
	{
		const TerrainMeshData& meshData = chunk->MeshData();
		if (!meshData.vertices.empty() && !meshData.indices.empty() && meshData.bHasBounds)
		{
			meshComponent.aabb = meshData.aabb;
			meshComponent.sphere = BoundingSphere(meshData.aabb);
			meshComponent.pVertices = const_cast< Vertex* >(meshData.vertices.data());
			meshComponent.numVertices = meshData.NumVertices();

			meshComponent.lods[0].pIndices = const_cast< Index* >(meshData.indices.data());
			meshComponent.lods[0].numIndices = meshData.NumIndices();
			if (!meshData.meshlets.empty())
			{
				meshComponent.lods[0].pMeshlets = const_cast< Meshlet* >(meshData.meshlets.data());
				meshComponent.lods[0].numMeshlets = meshData.NumMeshlets();
			}
			if (!meshData.meshletVertices.empty())
			{
				meshComponent.lods[0].pMeshletVertices = const_cast< u32* >(meshData.meshletVertices.data());
				meshComponent.lods[0].numMeshletVertices = static_cast< u32 >(meshData.meshletVertices.size());
			}
			if (!meshData.meshletTriangles.empty())
			{
				meshComponent.lods[0].pMeshletTriangles = const_cast< u32* >(meshData.meshletTriangles.data());
				meshComponent.lods[0].numMeshletTriangles = static_cast< u32 >(meshData.meshletTriangles.size());
			}
		}
	}

	m_pScene->Registry().patch< StaticMeshComponent >(m_VoxelTerrainEntity.ID(), [](auto&) {});
}

void TerrainApp::RefreshVoxelTerrainDebugLines(bool bSceneAlreadyLocked)
{
	if (!m_pScene)
		return;

	std::vector< DebugLineVertex > lines;
	const SDFChunk* chunk = m_VoxelTerrain.GetChunk(m_VoxelChunkIndex);
	if (!chunk)
	{
		if (bSceneAlreadyLocked)
			m_pScene->ClearDebugLinesAlreadyLocked();
		else
			m_pScene->ClearDebugLines();
		return;
	}

	const TerrainMeshData& meshData = chunk->MeshData();
	const float3 chunkOrigin = chunk->GetOriginWorld();

	if (m_bVoxelChunkBoundsVisible)
		AddAabbLines(lines, chunk->GetWorldBounds(), float3(1.0f, 0.84f, 0.22f), 1.0f);

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
					chunkOrigin + meshData.vertices[a].position,
					chunkOrigin + meshData.vertices[b].position,
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

			const float normalLength = glm::length(vertex.normal);
			if (!std::isfinite(normalLength) || normalLength <= 1e-6f)
				continue;

			const float3 start = chunkOrigin + vertex.position;
			const float3 end = start + (vertex.normal / normalLength) * m_VoxelNormalLineLength;
			AddDebugLine(lines, start, end, float3(0.25f, 1.0f, 0.35f), 1.0f);
		}
	}

	if (bSceneAlreadyLocked)
		m_pScene->SetDebugLinesAlreadyLocked(std::move(lines));
	else
		m_pScene->SetDebugLines(std::move(lines));
}
