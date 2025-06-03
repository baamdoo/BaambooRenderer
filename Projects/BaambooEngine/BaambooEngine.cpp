#include "BaambooPch.h"
#include "BaambooEngine.h"
#include "BaambooCore/Timer.h"
#include "BaambooCore/Window.h"
#include "BaambooCore/EngineCore.h"
#include "BaambooCore/Input.hpp"
#include "BaambooScene/Entity.h"
#include "BaambooScene/TransformSystem.h"
#include "ThreadQueue.hpp"

#include <filesystem>
#include <imgui/imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>
#include <glm/gtc/type_ptr.hpp>

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

	Timer timer{};
	m_RenderThread = std::thread(&Engine::RenderLoop, this);
	while (m_pWindow->PollEvent())
	{
		timer.Tick();

		auto dt = timer.GetDeltaSeconds();
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
		m_ResizeWidth = m_ResizeHeight = -1;
	}

	GameLoop(dt);
}

void Engine::Release()
{
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

		ImGui::DrawUI(*this);
		m_pRendererBackend->Render(std::move(renderView.value()));
	}
}

void Engine::DrawUI()
{
	// **
	// basic info
	// **
	ImGui::Begin("Engine Profile");
	{
		ImGui::Text("Application average %.3f ms(frame: %.1f FPS)", 1000.f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
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
