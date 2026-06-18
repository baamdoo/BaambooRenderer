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

#include <cstdio>

#include <glm/gtc/type_ptr.hpp>
#include <imgui/backends/imgui_impl_glfw.h>

using namespace baamboo;

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

	m_VoxelTerrainStats = VoxelTerrainDebug::CollectStats(m_VoxelTerrain);
	ImGui::Begin("Voxel Terrain");
	{
		if (ImGui::Button("Rebuild Samples"))
			RebuildVoxelTerrain();

		ImGui::Separator();
		ImGui::Text("Chunk Size      %.1f m", m_VoxelTerrainSettings.chunkWorldSizeMeter);
		ImGui::Text("Cells / Axis    %u", m_VoxelTerrainSettings.cellsPerAxis);
		ImGui::Text("Samples / Axis  %u", m_VoxelTerrainSettings.samplesPerAxis);
		ImGui::Text("Voxel Size      %.2f m", m_VoxelTerrainSettings.voxelSizeMeter);

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
	RebuildVoxelTerrain();

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

void TerrainApp::RebuildVoxelTerrain()
{
	m_VoxelTerrain.Clear();
	m_VoxelTerrain.Initialize(m_VoxelTerrainSettings);

	m_VoxelChunkIndex = m_VoxelTerrain.CreateChunk(float3(0.0f, 0.0f, 0.0f));
	m_VoxelTerrain.BuildChunkSamples(m_VoxelChunkIndex);
	m_VoxelTerrain.BuildChunkMesh(m_VoxelChunkIndex);

	m_VoxelTerrainStats = VoxelTerrainDebug::CollectStats(m_VoxelTerrain);
}
