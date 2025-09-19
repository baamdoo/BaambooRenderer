#include "BaambooPch.h"
#include "BaambooEngine.h"
#include "BaambooCore/Window.h"
#include "BaambooCore/EngineCore.h"
#include "BaambooCore/Input.hpp"
#include "BaambooScene/Entity.h"
#include "ThreadQueue.hpp"

#include <filesystem>
#include <imgui/imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>
#include <glm/gtc/type_ptr.hpp>
#include <magic_enum/magic_enum.hpp>

namespace ImGui
{

baamboo::Entity SelectedEntity;
baamboo::Entity EntityToCopy;
u32 ContentBrowserSetup = 0;

baamboo::ThreadQueue< baamboo::Entity > EntityDeletionQueue;

ImGuiContext* InitUI()
{
	IMGUI_CHECKVERSION();
	auto pContext = ImGui::CreateContext();
	ImGui::StyleColorsDark();

	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

	ImGuiStyle& style = ImGui::GetStyle();
	style.Colors[ImGuiCol_TitleBg] = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
	style.Colors[ImGuiCol_TitleBgActive] = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
	style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(1.0f, 0.0f, 0.0f, 0.1f);
	style.Colors[ImGuiCol_MenuBarBg] = ImVec4(1.0f, 0.0f, 0.0f, 0.4f);
	style.Colors[ImGuiCol_Header] = ImVec4(0.8f, 0.0f, 0.0f, 0.4f);
	style.Colors[ImGuiCol_HeaderActive] = ImVec4(1.0f, 0.0f, 0.0f, 0.4f);
	style.Colors[ImGuiCol_HeaderHovered] = ImVec4(1.0f, 0.0f, 0.0f, 0.4f);
	style.Colors[ImGuiCol_FrameBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.8f);
	style.Colors[ImGuiCol_CheckMark] = ImVec4(1.0f, 0.0f, 0.0f, 0.8f);
	style.Colors[ImGuiCol_SliderGrab] = ImVec4(1.0f, 0.0f, 0.0f, 0.4f);
	style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(1.0f, 0.0f, 0.0f, 0.8f);
	style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(1.0f, 1.0f, 1.0f, 0.1f);
	style.Colors[ImGuiCol_FrameBgActive] = ImVec4(1.0f, 1.0f, 1.0f, 0.2f);
	style.Colors[ImGuiCol_Button] = ImVec4(1.0f, 0.0f, 0.0f, 0.4f);
	style.Colors[ImGuiCol_ButtonHovered] = ImVec4(1.0f, 0.0f, 0.0f, 0.6f);
	style.Colors[ImGuiCol_ButtonActive] = ImVec4(1.0f, 0.0f, 0.0f, 0.8f);

	// subsequent initialization will be executed by window and renderer-backend

	return pContext;
}

void DrawUI(baamboo::Engine& engine)
{
	if (!engine.GetRenderer() || !engine.GetWindow())
		return;

	engine.GetRenderer()->NewFrame();
	engine.GetWindow()->NewFrame();
	ImGui::NewFrame();

	engine.DrawUI();
	ImGui::Render();
}

void Destroy()
{
	EntityToCopy.Reset();
	SelectedEntity.Reset();
	ContentBrowserSetup = 0;

	if (ImGui::GetCurrentContext())
		ImGui::DestroyContext();
}

} // namespace ImGui

namespace baamboo
{

enum
{
	eContentButton_None,
	eContentButton_Mesh,
	eContentButton_Albedo,
	eContentButton_Normal,
	eContentButton_Ao,
	eContentButton_Metallic,
	eContentButton_Roughness,
	eContentButton_Emission,
};

constexpr u32 NUM_TOLERANCE_ASYNC_FRAME_GAME_TO_RENDER = 3;

Engine::Engine()
	: m_CurrentDirectory(ASSET_PATH)
	, m_bRunning(false)
{
}

Engine::~Engine()
{
	Release();
}

void Engine::Initialize(eRendererAPI eApi)
{
	m_eBackendAPI = eApi;

	auto pImGuiContext = ImGui::InitUI();
	if (!InitWindow())
		throw std::runtime_error("Failed to initialize window!");
	if (!LoadRenderer(m_eBackendAPI, m_pWindow, pImGuiContext, &m_pRendererBackend))
		throw std::runtime_error("Failed to load backend!");
	if (!LoadScene())
		throw std::runtime_error("Failed to create scene!");
}

i32 Engine::Run()
{
	m_bRunning = true;
	m_RunningTime = 0.0;

	m_RenderThread = std::thread(&Engine::RenderLoop, this);
	while (m_pWindow->PollEvent())
	{
		m_GameTimer.Tick();

		auto dt        = m_GameTimer.GetDeltaSeconds();
		m_RunningTime += dt;

		Update(static_cast<float>(dt));

		Input::Inst()->EndFrame();
	}

	m_bRunning = false;
	return 0;
}

void Engine::Update(f32 dt)
{
	if (m_bWindowResized && m_ResizeWidth >= 0 && m_ResizeHeight >= 0)
	{
		if (m_ResizeWidth == 0 || m_ResizeHeight == 0)
			return;

		m_pWindow->OnWindowResized(m_ResizeWidth, m_ResizeHeight);
		m_pRendererBackend->OnWindowResized(m_ResizeWidth, m_ResizeHeight);

		m_bWindowResized = false;
		m_ResizeWidth    = m_ResizeHeight = -1;
	}

	GameLoop(dt);
}

void Engine::Release()
{
	Input::Inst()->Reset();

	m_RenderViewQueue.clear();
	if (m_RenderThread.joinable())
	{
		m_RenderThread.join();
	}

	RELEASE(m_pCamera);
	RELEASE(m_pScene);
	RELEASE(m_pRendererBackend);
	RELEASE(m_pWindow);

	ImGui::Destroy();
}

void Engine::GameLoop(float dt)
{
	ProcessInput();

	if (m_pScene == nullptr)
		return;

	std::lock_guard< std::mutex > lock(m_ImGuiMutex);

	m_pScene->Update(dt);

	if (m_RenderViewQueue.size() >= NUM_TOLERANCE_ASYNC_FRAME_GAME_TO_RENDER)
		m_RenderViewQueue.replace(m_pScene->RenderView(*m_pCamera));
	else
		m_RenderViewQueue.push(m_pScene->RenderView(*m_pCamera));
}

void Engine::RenderLoop()
{
	while (m_bRunning)
	{
		if (!ImGui::EntityDeletionQueue.empty())
		{
			std::lock_guard< std::mutex > lock(m_ImGuiMutex);

			auto entity = ImGui::EntityDeletionQueue.try_pop();
			m_pScene->RemoveEntity(entity.value());

			m_RenderViewQueue.clear();
		}

		auto renderView = m_RenderViewQueue.try_pop();
		if (!renderView.has_value())
			continue;

		m_RenderTimer.Tick();

		ImGui::DrawUI(*this);
		m_pRendererBackend->Render(std::move(renderView.value()));
	}
}

void Engine::DrawUI()
{
	// **
	// engine stats
	// **
	ImGui::Begin("Engine Stats");
	{
		static double gameElapsed_ms = m_GameTimer.GetDeltaMilliseconds();
		if (m_GameTimer.GetTotalSeconds() > 1.0f)
		{
			gameElapsed_ms = m_GameTimer.GetDeltaMilliseconds();

			m_GameTimer.Reset();
		}
		ImGui::Text("GameLoop   %.3f ms(frame: %.1f FPS)", gameElapsed_ms, 1000.0f / gameElapsed_ms);

		static double renderElapsed_ms = m_RenderTimer.GetDeltaMilliseconds();
		if (m_RenderTimer.GetTotalSeconds() > 1.0f)
		{
			renderElapsed_ms = m_RenderTimer.GetDeltaMilliseconds();

			m_RenderTimer.Reset();
		}
		ImGui::Text("RenderLoop %.3f ms(frame: %.1f FPS)", renderElapsed_ms, 1000.0f / renderElapsed_ms);
	}
	ImGui::End();


	// **
	// scene hierarchy panel
	// **
	if (!m_pScene)
		return;

	ImGui::Begin("Scene Hierarchy");
	{
		if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows))
		{
			if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_N))
			{
				ImGui::SelectedEntity = m_pScene->CreateEntity("Empty");
			}

			if (ImGui::Shortcut(ImGuiKey_Delete))
			{
				if (ImGui::SelectedEntity.IsValid())
				{
					// execute next frame to avoid data hazard
					ImGui::EntityDeletionQueue.push(std::move(ImGui::SelectedEntity));
				}
			}

			if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_C))
			{
				ImGui::EntityToCopy = ImGui::SelectedEntity;
			}

			if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_V))
			{
				if (ImGui::EntityToCopy.IsValid())
				{
					ImGui::SelectedEntity = ImGui::EntityToCopy.Clone();
				}
			}
		}

		m_pScene->Registry().view< TagComponent, TransformComponent >().each([this](auto id, auto& tagComponent, auto& transformComponent)
			{
				if (transformComponent.hierarchy.parent == entt::null)
				{
					auto entity = Entity(m_pScene, id);
					DrawEntityNode(entity);
				}
			});

		ImVec2 availRegion = ImGui::GetContentRegionAvail();
		ImGui::InvisibleButton("EmptyAreaDropTarget", ImVec2(availRegion.x, availRegion.y > 50 ? availRegion.y : 50));
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENTITY_HIERARCHY"))
			{
				entt::entity droppedEntity = *(entt::entity*)payload->Data;

				auto entity = Entity(m_pScene, droppedEntity);
				entity.DetachChild();
			}
			ImGui::EndDragDropTarget();
		}
	}
	ImGui::End();


	// **
	// component panel
	// **
	if (ImGui::SelectedEntity.IsValid())
	{
		ImGui::Begin("Components");
		{
			if (ImGui::CollapsingHeader("Tag"))
			{
				auto& tag = ImGui::SelectedEntity.GetComponent< TagComponent >().tag;
				ImGui::InputText("Name", &tag);
			}
			
			if (ImGui::SelectedEntity.HasAll< TransformComponent >())
			{
				bool bMark = false;

				if (ImGui::CollapsingHeader("Transform"))
				{
					auto& transformComponent = ImGui::SelectedEntity.GetComponent< TransformComponent >();

					ImGui::Text("Position");
					bMark |= ImGui::DragFloat3("##Position", glm::value_ptr(transformComponent.transform.position), 0.1f);

					ImGui::Text("Rotation");
					bMark |= ImGui::DragFloat3("##Rotation", glm::value_ptr(transformComponent.transform.rotation), 0.1f);

					ImGui::Text("Scale");
					bMark |= ImGui::DragFloat3("##Scale", glm::value_ptr(transformComponent.transform.scale), 0.1f);
				}

				if (bMark)
				{
					m_pScene->Registry().patch< TransformComponent >(ImGui::SelectedEntity.ID(), [](auto&) {});
				}
			}

			if (ImGui::SelectedEntity.HasAll< CameraComponent >())
			{
				bool bMark = false;
				if (ImGui::CollapsingHeader("Camera"))
				{
					auto& cameraComponent = ImGui::SelectedEntity.GetComponent< CameraComponent >();

					ImGui::Text("CameraType");
					if (ImGui::BeginCombo("##CameraType", GetCameraTypeString(cameraComponent.type).data()))
					{
						if (ImGui::Selectable(GetCameraTypeString(CameraComponent::eType::Perspective).data(), cameraComponent.type == CameraComponent::eType::Perspective))
						{
							bMark = true;
							cameraComponent.type = CameraComponent::eType::Perspective;
						}

						if (ImGui::Selectable(GetCameraTypeString(CameraComponent::eType::Orthographic).data(), cameraComponent.type == CameraComponent::eType::Orthographic))
						{
							bMark = true;
							cameraComponent.type = CameraComponent::eType::Orthographic;
						}

						ImGui::EndCombo();
					}

					float width = (ImGui::GetWindowWidth() - ImGui::GetStyle().ItemSpacing.x);

					ImGui::PushItemWidth(width * 0.3f);
					ImGui::Text("ClippingRange");
					bMark |= ImGui::InputFloat("##ClipNear", &cameraComponent.cNear, 0, 0, "%.2f");

					ImGui::PushItemWidth(width * 0.7f);
					ImGui::SameLine();
					bMark |= ImGui::InputFloat("##ClipFar", &cameraComponent.cFar, 0, 0, "%.2f");

					ImGui::Text("FoV");
					bMark |= ImGui::DragFloat("##FoV", &cameraComponent.fov, 0.1f, 1.0f, 90.0f, "%.1f");

					bool bMain = cameraComponent.bMain;
					ImGui::Checkbox("MainCam", &bMain);
				}

				if (bMark)
				{
					m_pScene->Registry().patch< CameraComponent >(ImGui::SelectedEntity.ID(), [](auto&) {});
				}
			}

			if (ImGui::SelectedEntity.HasAll< StaticMeshComponent >())
			{
				if (ImGui::CollapsingHeader("StaticMesh"))
				{
					auto& component = ImGui::SelectedEntity.GetComponent< StaticMeshComponent >();

					if (ImGui::Button("Mesh")) ImGui::ContentBrowserSetup = eContentButton_Mesh;
					ImGui::SameLine(); ImGui::Text(component.path.c_str());
				}

				if (ImGui::SelectedEntity.HasAll< MaterialComponent >())
				{
					bool bMark = false;
					if (ImGui::CollapsingHeader("Material"))
					{
						auto& component = ImGui::SelectedEntity.GetComponent< MaterialComponent >();

						ImGui::Text("Tint");
						bMark |= ImGui::DragFloat3("##Tint", glm::value_ptr(component.tint), 0.01f, 0.0f, 1.0f, "%.2f");
						ImGui::Text("Roughess");
						bMark |= ImGui::DragFloat("##Roughness", &component.roughness, 0.01f, 0.0f, 1.0f, "%.2f");
						ImGui::Text("Metallic");
						bMark |= ImGui::DragFloat("##Metallic", &component.metallic, 0.01f, 0.0f, 1.0f, "%.2f");

						if (ImGui::Button("AlbedoTex")) ImGui::ContentBrowserSetup = eContentButton_Albedo;
						ImGui::SameLine(); ImGui::Text(component.albedoTex.c_str());
						if (ImGui::Button("NormalTex")) ImGui::ContentBrowserSetup = eContentButton_Normal;
						ImGui::SameLine(); ImGui::Text(component.normalTex.c_str());
						if (ImGui::Button("AoTex")) ImGui::ContentBrowserSetup = eContentButton_Ao;
						ImGui::SameLine(); ImGui::Text(component.aoTex.c_str());
						if (ImGui::Button("RoughnessTex")) ImGui::ContentBrowserSetup = eContentButton_Roughness;
						ImGui::SameLine(); ImGui::Text(component.roughnessTex.c_str());
						if (ImGui::Button("MetallicTex")) ImGui::ContentBrowserSetup = eContentButton_Metallic;
						ImGui::SameLine(); ImGui::Text(component.metallicTex.c_str());
						if (ImGui::Button("EmissionTex")) ImGui::ContentBrowserSetup = eContentButton_Emission;
						ImGui::SameLine(); ImGui::Text(component.emissionTex.c_str());
					}

					if (bMark)
					{
						m_pScene->Registry().patch< MaterialComponent >(ImGui::SelectedEntity.ID(), [](auto&) {});
					}
				}
			}

			if (ImGui::SelectedEntity.HasAll< LightComponent >())
			{
				if (ImGui::CollapsingHeader("Light"))
				{
					auto& component = ImGui::SelectedEntity.GetComponent< LightComponent >();

					const char* lightTypes[] = { "Directional", "Point", "Spot" };
					int currentType = (int)component.type;
					if (ImGui::Combo("Type", &currentType, lightTypes, IM_ARRAYSIZE(lightTypes)))
					{
						component.type = (eLightType)currentType;

						switch (component.type)
						{
						case eLightType::Directional:
							component.SetDefaultDirectionalLight();
							break;
						case eLightType::Point:
							component.SetDefaultPoint();
							break;
						case eLightType::Spot:
							component.SetDefaultSpot();
							break;
						}
					}

					ImGui::ColorEdit3("Color", &component.color.x);

					if (ImGui::DragFloat("Temperature (K)", &component.temperature_K, 10.0f, 0.0f, 10000.0f))
					{
						// Temperature of 0 means use RGB color
					}
					if (ImGui::IsItemHovered())
					{
						ImGui::BeginTooltip();
						ImGui::Text("Use RGB color if temperature is 0");
						ImGui::EndTooltip();
					}

					switch (component.type)
					{

					case eLightType::Directional:
					{
						ImGui::DragFloat("Illuminance (lux)", &component.illuminance_lux, 0.1f, 0.0f, 10.0f, "%.1f");
						if (ImGui::IsItemHovered())
						{
							ImGui::BeginTooltip();
							ImGui::EndTooltip();
						}

						float angularRadius = glm::degrees(component.angularRadius_rad);
						if (ImGui::DragFloat("Angular Radius (deg)", &angularRadius, 0.01f, 0.0f, 10.0f, "%.3f"))
						{
							component.angularRadius_rad = glm::radians(angularRadius);
						}
						break;
					}
					case eLightType::Point:
					{
						ImGui::DragFloat("Power (lm)", &component.luminousFlux_lm, 10.0f, 0.0f, 10000.0f, "%.0f");
						if (ImGui::IsItemHovered())
						{
							ImGui::BeginTooltip();
							ImGui::EndTooltip();
						}
						ImGui::DragFloat("Source Radius (m)", &component.radius_m, 0.001f, 0.001f, 1.0f, "%.3f");
						break;
					}
					case eLightType::Spot:
					{
						ImGui::DragFloat("Power (lumens)", &component.luminousFlux_lm, 10.0f, 0.0f, 10000.0f, "%.0f");
						ImGui::DragFloat("Source Radius (m)", &component.radius_m, 0.001f, 0.001f, 1.0f, "%.3f");

						float innerAngle = glm::degrees(component.innerConeAngle_rad);
						float outerAngle = glm::degrees(component.outerConeAngle_rad);

						if (ImGui::DragFloat("Inner Angle (deg)", &innerAngle, 1.0f, 0.0f, 90.0f, "%.1f"))
						{
							component.innerConeAngle_rad = glm::radians(innerAngle);

							if (component.outerConeAngle_rad <= component.innerConeAngle_rad)
								component.outerConeAngle_rad = component.innerConeAngle_rad + glm::radians(1.0f);
						}

						if (ImGui::DragFloat("Outer Angle (deg)", &outerAngle, 1.0f, 0.0f, 90.0f, "%.1f"))
						{
							component.outerConeAngle_rad = glm::radians(outerAngle);

							if (component.innerConeAngle_rad >= component.outerConeAngle_rad)
								component.innerConeAngle_rad = component.outerConeAngle_rad - glm::radians(1.0f);
						}
						break;
					}

					}

					ImGui::DragFloat("EV100", &component.ev100, 0.01f, -15.0f, 15.0f, "%.2f");
				}
			}

			if (ImGui::SelectedEntity.HasAll< AtmosphereComponent >())
			{
				bool bMark = false;
				if (ImGui::CollapsingHeader("Atmosphere"))
				{
					ImGui::Indent();
					{
						auto& component = ImGui::SelectedEntity.GetComponent< AtmosphereComponent >();

						// planet
						if (ImGui::CollapsingHeader("Planet"))
						{
							float height = component.atmosphereRadius_km - component.planetRadius_km;
							if (ImGui::DragFloat("Atmosphere Height (km)", &height, 1.0f, 1.0f, 200.0f, "%.1f"))
							{
								component.atmosphereRadius_km = component.planetRadius_km + height;

								bMark = true;
							}
						}

						// rayleigh
						if (ImGui::CollapsingHeader("Rayleigh"))
						{
							float3 rayleighScattering = component.rayleighScattering * 1e3f;
							if (ImGui::DragFloat3("Rayleigh Scattering Coefficient", glm::value_ptr(rayleighScattering), 0.01f, 0.0f, 1000.0f, "%.3f"))
							{
								component.rayleighScattering = rayleighScattering * 1e-3f;

								bMark = true;
							}
							if (ImGui::IsItemHovered())
							{
								ImGui::BeginTooltip();
								ImGui::Text("Rayleigh Scattering Scale : 0.001");
								ImGui::EndTooltip();
							}
							bMark |= ImGui::DragFloat("Rayleigh Density (km)", &component.rayleighDensityH_km, 0.1f, 0.1f, 20.0f, "%.3f");
							if (ImGui::IsItemHovered())
							{
								ImGui::BeginTooltip();
								ImGui::Text("Reduce 40%% of scattering effect per Km");
								ImGui::EndTooltip();
							}
						}

						// mie
						if (ImGui::CollapsingHeader("Mie"))
						{
							float mieScattering = component.mieScattering * 1e3f;
							if (ImGui::DragFloat("Mie Scattering Coefficient", &mieScattering, 0.01f, 0.0f, 1000.0f, "%.3f"))
							{
								component.mieScattering = mieScattering * 1e-3f;

								bMark = true;
							}
							if (ImGui::IsItemHovered())
							{
								ImGui::BeginTooltip();
								ImGui::Text("Mie Scattering Scale : 0.001");
								ImGui::EndTooltip();
							}
							float mieAbsorption = component.mieAbsorption * 1e3f;
							if (ImGui::DragFloat("Mie Absorption Coefficient", &mieAbsorption, 0.01f, 0.0f, 1000.0f, "%.3f"))
							{
								component.mieAbsorption = mieAbsorption * 1e-3f;

								bMark = true;
							}
							if (ImGui::IsItemHovered())
							{
								ImGui::BeginTooltip();
								ImGui::Text("Mie Absorption Scale : 0.001");
								ImGui::EndTooltip();
							}
							bMark |= ImGui::DragFloat("Mie Density (km)", &component.mieDensityH_km, 0.01f, 0.01f, 10.0f, "%.3f");
							if (ImGui::IsItemHovered())
							{
								ImGui::BeginTooltip();
								ImGui::Text("Reduce 40%% of mie effect per Km");
								ImGui::EndTooltip();
							}
							bMark |= ImGui::DragFloat("Mie Phase", &component.miePhaseG, 0.01f, 0.0f, 1.0f, "%.3f");
						}

						// ozone
						if (ImGui::CollapsingHeader("Ozone"))
						{
							float3 ozoneAbsorption = component.ozoneAbsorption * 1e3f;
							if (ImGui::DragFloat3("Ozone Absorption Coefficient", glm::value_ptr(ozoneAbsorption), 0.01f, 0.0f, 1000.0f, "%.3f"))
							{
								component.ozoneAbsorption = ozoneAbsorption * 1e-3f;

								bMark = true;
							}
							if (ImGui::IsItemHovered())
							{
								ImGui::BeginTooltip();
								ImGui::Text("Ozone Absorption Scale : 0.001");
								ImGui::EndTooltip();
							}
							bMark |= ImGui::DragFloat("Ozone Center (km)", &component.ozoneCenter_km, 1.0f, 1.0f, 60.0f, "%.1f");
							bMark |= ImGui::DragFloat("Ozone Width (km)", &component.ozoneWidth_km, 1.0f, 1.0f, 20.0f, "%.1f");

							if (ImGui::BeginCombo("##Resolution", "Raymarch Resolution"))
							{
								if (ImGui::Selectable("Low", component.raymarchResolution == eRaymarchResolution::Low))
								{
									component.raymarchResolution = eRaymarchResolution::Low;

									bMark = true;
								}
								if (ImGui::Selectable("Middle", component.raymarchResolution == eRaymarchResolution::Middle))
								{
									component.raymarchResolution = eRaymarchResolution::Middle;

									bMark = true;
								}
								if (ImGui::Selectable("High", component.raymarchResolution == eRaymarchResolution::High))
								{
									component.raymarchResolution = eRaymarchResolution::High;

									bMark = true;
								}

								ImGui::EndCombo();
							}
						}
					}

					ImGui::Unindent();
				}

				if (bMark)
				{
					m_pScene->Registry().patch< AtmosphereComponent >(ImGui::SelectedEntity.ID(), [](auto&) {});
				}
			}

			if (ImGui::SelectedEntity.HasAll< CloudComponent >())
			{
				bool bMark = false;
				if (ImGui::CollapsingHeader("Cloud"))
				{
					ImGui::Indent();
					{
						auto& component = ImGui::SelectedEntity.GetComponent< CloudComponent >();

						if (ImGui::CollapsingHeader("Shape"))
						{
							bMark |= ImGui::DragFloat("Cloud Coverage", &component.coverage, 0.001f, 0.0f, 1.0f, "%.3f");
							bMark |= ImGui::DragFloat("Cloud Type", &component.cloudType, 0.01f, 0.0f, 1.0f, "%.2f");
							bMark |= ImGui::DragFloat("Cloud Precipitation", &component.precipitation, 0.01f, 0.0f, 1.0f, "%.2f");
							bMark |= ImGui::DragFloat("Cloud Bottom Height (km)", &component.bottomHeight_km, 0.1f, 0.0f, 10.0f, "%.1f");
							bMark |= ImGui::DragFloat("Cloud Thickness (km)", &component.layerThickness_km, 0.1f, 0.1f, 100.0f, "%.1f");

							bMark |= ImGui::DragFloat("Base Scale", &component.baseNoiseScale, 0.001f, 0.001f, 1.0f, "%.3f");
							if (ImGui::IsItemHovered())
							{
								ImGui::BeginTooltip();
								ImGui::Text("Range(km) per tile");
								ImGui::EndTooltip();
							}
							bMark |= ImGui::DragFloat("Base Intensity", &component.baseIntensity, 0.01f, 0.0f, 10.0f, "%.2f");

							bMark |= ImGui::DragFloat("Detail Scale", &component.detailNoiseScale, 0.001f, 0.001f, 1.0f, "%.3f");
							if (ImGui::IsItemHovered())
							{
								ImGui::BeginTooltip();
								ImGui::Text("Range(km) per tile");
								ImGui::EndTooltip();
							}
							bMark |= ImGui::DragFloat("Detail Intensity", &component.detailIntensity, 0.01f, 0.0f, 1.0f, "%.2f");

							if (ImGui::DragFloat3("Wind Direction", glm::value_ptr(component.windDirection), 0.01f, 0.0f, 1.0f, "%.2f"))
							{
								component.windDirection = glm::normalize(component.windDirection);

								bMark = true;
							}
							bMark |= ImGui::DragFloat("Wind Speed(m/s)", &component.windSpeed_mps, 0.1f, 0.0f, 1000.0f, "%.1f");
						}
					}

					ImGui::Unindent();
				}

				if (bMark)
				{
					m_pScene->Registry().patch< CloudComponent >(ImGui::SelectedEntity.ID(), [](auto&) {});
				}
			}

			if (ImGui::SelectedEntity.HasAll< PostProcessComponent >())
			{
				if (ImGui::CollapsingHeader("PostProcess"))
				{
					bool bMark = false;
					ImGui::Indent();
					{
						auto& component = ImGui::SelectedEntity.GetComponent< PostProcessComponent >();
						if (ImGui::CollapsingHeader("Height Fog(TODO)"))
						{
							bool bApply = component.effectBits & ePostProcess::HeightFog;
							bMark |= ImGui::Checkbox("Apply HeightFog", &bApply);
							component.effectBits =
								(component.effectBits & ~(1 << ePostProcess::AntiAliasing)) | (bApply << ePostProcess::HeightFog);

							ImGui::DragFloat("ExponentialFactor", &component.heightFog.exponentialFactor, 0.1f, 0.0f, 20.0f, "%.1f");
						}

						if (ImGui::CollapsingHeader("Bloom(TODO)"))
						{
							bool bApply = component.effectBits & ePostProcess::Bloom;
							bMark |= ImGui::Checkbox("Apply Bloom", &bApply);
							component.effectBits =
								(component.effectBits & ~(1 << ePostProcess::AntiAliasing)) | (bApply << ePostProcess::Bloom);

							ImGui::DragInt("FilterSize", &component.bloom.filterSize, 1, 1, 16);
						}

						if (ImGui::CollapsingHeader("AntiAliasing"))
						{
							bool bApply = component.effectBits & (1 << ePostProcess::AntiAliasing);
							bMark |= ImGui::Checkbox("Apply Anti-Aliasing", &bApply);
							component.effectBits =
								(component.effectBits & ~(1 << ePostProcess::AntiAliasing)) | (bApply << ePostProcess::AntiAliasing);

							auto svCurrentType = magic_enum::enum_name(component.aa.type);
							if (ImGui::BeginCombo("AntiAliasing Type", svCurrentType.data()))
							{
								if (ImGui::Selectable("TAA", component.aa.type == eAntiAliasingType::TAA))
								{
									component.aa.type = eAntiAliasingType::TAA;

									bMark = true;
								}
								if (ImGui::Selectable("FXAA", component.aa.type == eAntiAliasingType::FXAA))
								{
									component.aa.type = eAntiAliasingType::FXAA;

									bMark = true;
								}

								ImGui::EndCombo();
							}

							if (component.aa.type == eAntiAliasingType::TAA)
							{
								bMark |= ImGui::DragFloat("Blend Factor", &component.aa.blendFactor, 0.01f, 0.0f, 1.0f, "%.2f");
								bMark |= ImGui::DragFloat("Sharpness", &component.aa.sharpness, 0.01f, 0.0f, 1.0f, "%.2f");
							}
						}

						if (ImGui::CollapsingHeader("ToneMapping"))
						{
							auto svCurrentOp = magic_enum::enum_name(component.tonemap.op);
							if (ImGui::BeginCombo("ToneMap Operation", svCurrentOp.data()))
							{
								if (ImGui::Selectable("Reinhard", component.tonemap.op == eToneMappingOp::Reinhard))
								{
									component.tonemap.op = eToneMappingOp::Reinhard;

									bMark = true;
								}
								if (ImGui::Selectable("ACES", component.tonemap.op == eToneMappingOp::ACES))
								{
									component.tonemap.op = eToneMappingOp::ACES;

									bMark = true;
								}
								if (ImGui::Selectable("Uncharted2", component.tonemap.op == eToneMappingOp::Uncharted2))
								{
									component.tonemap.op = eToneMappingOp::Uncharted2;

									bMark = true;
								}

								ImGui::EndCombo();
							}

							bMark |= ImGui::DragFloat("Gamma", &component.tonemap.gamma, 0.1f, 0.1f, 10.0f, "%.1f");
						}
					}
					ImGui::Unindent();

					if (bMark)
					{
						m_pScene->Registry().patch< PostProcessComponent >(ImGui::SelectedEntity.ID(), [](auto&) {});
					}
				}
			}

			if (ImGui::SelectedEntity.HasAll< ScriptComponent >())
			{
				if (ImGui::CollapsingHeader("Behaviour"))
				{
					auto& scriptComponent = ImGui::SelectedEntity.GetComponent< ScriptComponent >();

					ImGui::Checkbox("Move", &scriptComponent.bMove);
					ImGui::DragFloat3("MoveVelocity", glm::value_ptr(scriptComponent.moveVelocity), 0.1f, -10.0f, 10.0f);

					ImGui::Checkbox("Rotate", &scriptComponent.bRotate);
					ImGui::DragFloat3("RotationVelocity", glm::value_ptr(scriptComponent.rotationVelocity), 0.01f, -1.0f, 1.0f);
				}
			}

			if (ImGui::Button("Add Component"))
				ImGui::OpenPopup("AddComponentPopup");

			if (ImGui::BeginPopup("AddComponentPopup")) 
			{
				/*if (!ImGui::SelectedEntity.HasAny< CameraComponent >())
				{
					if (ImGui::MenuItem("Camera"))
					{
						m_ImGuiMutex.lock();

						ImGui::SelectedEntity.AttachComponent< CameraComponent >();

						m_ImGuiMutex.unlock();
					}
				}*/

				if (!ImGui::SelectedEntity.HasAny< StaticMeshComponent >())
				{
					if (ImGui::MenuItem("StaticMesh"))
					{
						m_ImGuiMutex.lock();

						ImGui::SelectedEntity.AttachComponent< StaticMeshComponent >();

						m_ImGuiMutex.unlock();
					}
				}
				else if (!ImGui::SelectedEntity.HasAny< MaterialComponent >())
				{
					if (ImGui::MenuItem("Material"))
					{
						m_ImGuiMutex.lock();

						ImGui::SelectedEntity.AttachComponent< MaterialComponent >();

						m_ImGuiMutex.unlock();
					}
				}

				ImGui::EndPopup();
			}
		}
		ImGui::End();
	}

	// apply script behaviour
	m_pScene->Registry().view< TransformComponent, ScriptComponent >().each([this](auto id, auto& transformComponent, auto& scriptComponent)
		{
			if (scriptComponent.bMove)
			{
				transformComponent.transform.position += scriptComponent.moveVelocity;

				m_pScene->Registry().patch< TransformComponent >(ImGui::SelectedEntity.ID(), [](auto&) {});
			}

			if (scriptComponent.bRotate)
			{
				transformComponent.transform.Rotate(scriptComponent.rotationVelocity.y, scriptComponent.rotationVelocity.x, scriptComponent.rotationVelocity.z);

				m_pScene->Registry().patch< TransformComponent >(ImGui::SelectedEntity.ID(), [](auto&) {});
			}
		});


	// **
	// content browser
	// **
	if (ImGui::ContentBrowserSetup)
	{
		ImGui::Begin("Content Browser");
		{
			if (m_CurrentDirectory != fs::path(ASSET_PATH))
			{
				if (ImGui::Button("<"))
				{
					m_CurrentDirectory = m_CurrentDirectory.parent_path();
				}
			}

			for (auto& entry : std::filesystem::directory_iterator(m_CurrentDirectory))
			{
				const auto& path = entry.path();
				auto relativePath = fs::relative(entry.path(), ASSET_PATH);
				auto filenameStr = relativePath.filename().string();
				if (entry.is_directory())
				{
					if (ImGui::Button(filenameStr.c_str()))
					{
						m_CurrentDirectory /= path.filename();
					}
				}
				else
				{
					std::string extensionStr = path.extension().string();

					switch(ImGui::ContentBrowserSetup)
					{
					case eContentButton_Mesh:
						if (extensionStr == ".fbx" || extensionStr == ".obj" || extensionStr == ".gltf")
						{
							bool bMark = false;
							if (ImGui::Selectable(filenameStr.c_str()))
							{
								if (ImGui::SelectedEntity.HasAll< StaticMeshComponent >())
								{
									auto& component = ImGui::SelectedEntity.GetComponent< StaticMeshComponent >();
									if (component.path != path.string())
									{
										m_pScene->ImportModel(ImGui::SelectedEntity, path, {});

										component.path = path.string();
										bMark = true;
									}
								}

								ImGui::ContentBrowserSetup = 0;
							}

							if (bMark)
							{
								m_pScene->Registry().patch< StaticMeshComponent >(ImGui::SelectedEntity.ID(), [](auto&) {});
							}
						}
						break;

					case eContentButton_Albedo:
					case eContentButton_Normal:
					case eContentButton_Ao:
					case eContentButton_Metallic:
					case eContentButton_Roughness:
					case eContentButton_Emission:
						if (extensionStr == ".png" || extensionStr == ".jpg")
						{
							bool bMark = false;
							if (ImGui::Selectable(filenameStr.c_str())) 
							{
								if (ImGui::SelectedEntity.HasAll< StaticMeshComponent, MaterialComponent >())
								{
									auto& component = ImGui::SelectedEntity.GetComponent< MaterialComponent >();
									switch(ImGui::ContentBrowserSetup)
									{
									case eContentButton_Albedo:
										if (component.albedoTex != path.string())
										{
											component.albedoTex = path.string();
											bMark = true;
										}
										break;
									case eContentButton_Normal:
										if (component.normalTex != path.string())
										{
											component.normalTex = path.string();
											bMark = true;
										}
										break;
									case eContentButton_Ao:
										if (component.aoTex != path.string())
										{
											component.aoTex = path.string();
											bMark = true;
										};
										break;
									case eContentButton_Metallic:
										if (component.metallicTex != path.string())
										{
											component.metallicTex = path.string();
											bMark = true;
										}
										break;
									case eContentButton_Roughness:
										if (component.roughnessTex != path.string())
										{
											component.roughnessTex = path.string();
											bMark = true;
										}
										break;
									case eContentButton_Emission:
										if (component.emissionTex != path.string())
										{
											component.emissionTex = path.string();
											bMark = true;
										}
										break;
									default:
										break;
									}
								}

								ImGui::ContentBrowserSetup = 0;
							}

							if (bMark)
							{
								m_pScene->Registry().patch< MaterialComponent >(ImGui::SelectedEntity.ID(), [](auto&) {});
							}
						}
						break;
					default:
						break;
					}
				}
			}
		}
		ImGui::End();
	}
}

void Engine::DrawEntityNode(Entity entity)
{
	const auto& registry = m_pScene->Registry();

	const auto& tag = registry.get< TagComponent >(entity).tag;
	auto& hierarchy = registry.get< TransformComponent >(entity).hierarchy;

	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow;
	if (ImGui::SelectedEntity == entity) 
		flags |= ImGuiTreeNodeFlags_Selected;
	if (hierarchy.firstChild == entt::null) 
		flags |= ImGuiTreeNodeFlags_Leaf;

	auto id = entity.ID();
	bool bOpen = ImGui::TreeNodeEx((void*)(u64)id, flags, "%s", tag.c_str());

	if (ImGui::BeginDragDropSource())
	{
		ImGui::SetDragDropPayload("ENTITY_HIERARCHY", &id, sizeof(entt::entity));
		ImGui::Text("Moving Entity %d", (int)id);
		ImGui::EndDragDropSource();
	}

	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENTITY_HIERARCHY"))
		{
			entt::entity droppedEntity = *(entt::entity*)payload->Data;
			entity.AttachChild(droppedEntity);
		}
		ImGui::EndDragDropTarget();
	}

	if (ImGui::IsItemClicked())
		ImGui::SelectedEntity = entity;

	if (bOpen)
	{
		entt::entity child = hierarchy.firstChild;
		while (child != entt::null)
		{
			DrawEntityNode(Entity{ m_pScene, child });
			child = registry.get< TransformComponent >(child).hierarchy.nextSibling;
		}
		ImGui::TreePop();
	}
}

void Engine::ProcessInput()
{
	if (glfwGetKey(m_pWindow->Handle(), GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS && glfwGetKey(m_pWindow->Handle(), GLFW_KEY_V))
	{
		if (m_eBackendAPI != eRendererAPI::Vulkan)
		{
			m_bRunning = false;
			// NOTE. There is a bug which the window image is not properly updated 
			//       i.e. the last image output by d3d12 renderer remains intact.
			//       While the rendering-to-present process is executed normally(according to RenderDoc and PIX).
			//       It is hard to debug. So bypassed by window recreation for now.
			Release();
			Initialize(eRendererAPI::Vulkan);

			m_bRunning = true;
			m_RenderThread = std::thread(&Engine::RenderLoop, this);
		}
	}
	else if (glfwGetKey(m_pWindow->Handle(), GLFW_KEY_LEFT_SHIFT) && glfwGetKey(m_pWindow->Handle(), GLFW_KEY_D))
	{
		if (m_eBackendAPI != eRendererAPI::D3D12)
		{
			m_bRunning = false;

			Release();
			Initialize(eRendererAPI::D3D12);

			m_bRunning = true;
			m_RenderThread = std::thread(&Engine::RenderLoop, this);
		}
	}
}

} // namespace baamboo
