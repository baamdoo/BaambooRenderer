#include "BaambooPch.h"
#include "BaambooEngine.h"
#include "BaambooCore/Timer.h"
#include "BaambooCore/Window.h"
#include "BaambooCore/EngineCore.h"
#include "World/Entity.h"
#include "World/TransformSystem.h"

#include <filesystem>
#include <imgui/imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>
#include <glm/gtc/type_ptr.hpp>

namespace ImGui
{

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
	if (ImGui::GetCurrentContext())
		ImGui::DestroyContext();
}

baamboo::Entity selectedEntity;
baamboo::Entity entityToCopy;
u32 contentBrowserSetup = 0;

} // namespace ImGui

namespace baamboo
{

enum
{
	eContentButton_None,
	eContentButton_Texture,
	eContentButton_Geometry,
};

Engine::Engine()
	: m_currentDirectory(ASSET_PATH)
{
}

Engine::~Engine()
{
	RELEASE(m_pScene);
	RELEASE(m_pRendererBackend);
	RELEASE(m_pWindow);

	ImGui::Destroy();
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
		throw std::runtime_error("Failed to create world!");

}

i32 Engine::Run()
{
	m_runningTime = 0.0;

	Timer timer{};
	while (m_pWindow->PollEvent())
	{
		timer.Tick();

		auto dt = timer.GetDeltaSeconds();
		m_runningTime += dt;

		Update(static_cast<float>(dt));
	}

	return 0;
}

void Engine::Update(f32 dt)
{
	if (m_bWindowResized && m_resizeWidth > 0 && m_resizeHeight > 0)
	{
		// TODO: handle negative viewport
		m_pWindow->OnWindowResized(m_resizeWidth, m_resizeHeight);
		m_pRendererBackend->OnWindowResized(m_resizeWidth, m_resizeHeight);

		m_bWindowResized = false;
		m_resizeWidth = m_resizeHeight = 0;
	}

	ProcessInput();

	m_pScene->Update(dt);

	ImGui::DrawUI(*this);
	m_pRendererBackend->Render();
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
				ImGui::selectedEntity = m_pScene->CreateEntity("Empty");
			}

			if (ImGui::Shortcut(ImGuiKey_Delete))
			{
				if (ImGui::selectedEntity.IsValid())
				{
					m_pScene->RemoveEntity(ImGui::selectedEntity);
					ImGui::selectedEntity.Reset();
				}
			}

			if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_C))
			{
				ImGui::entityToCopy = ImGui::selectedEntity;
			}

			if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_V))
			{
				if (ImGui::entityToCopy.IsValid())
				{
					ImGui::selectedEntity = ImGui::entityToCopy.Clone();
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
	if (ImGui::selectedEntity.IsValid())
	{
		ImGui::Begin("Components");
		{
			if (ImGui::CollapsingHeader("Tag"))
			{
				auto& tag = ImGui::selectedEntity.GetComponent< TagComponent >().tag;
				ImGui::InputText("Name", &tag);
			}
			
			if (ImGui::selectedEntity.HasAll< TransformComponent >())
			{
				if (ImGui::CollapsingHeader("Transform"))
				{
					auto& transformComponent = ImGui::selectedEntity.GetComponent< TransformComponent >();

					ImGui::Text("Position");
					ImGui::DragFloat3("##Position", glm::value_ptr(transformComponent.transform.position), 0.1f);

					ImGui::Text("Rotation");
					ImGui::DragFloat3("##Rotation", glm::value_ptr(transformComponent.transform.rotation), 0.1f);

					ImGui::Text("Scale");
					ImGui::DragFloat3("##Scale", glm::value_ptr(transformComponent.transform.scale), 0.1f);
				}
			}

			if (ImGui::selectedEntity.HasAll< StaticMeshComponent >())
			{
				if (ImGui::CollapsingHeader("StaticMesh"))
				{
					auto& component = ImGui::selectedEntity.GetComponent< StaticMeshComponent >();
					
					if (ImGui::Button("Texture")) ImGui::contentBrowserSetup = eContentButton_Texture;
					ImGui::SameLine(); ImGui::Text(component.texture.c_str());
					if (ImGui::Button("Geometry")) ImGui::contentBrowserSetup = eContentButton_Geometry;
					ImGui::SameLine(); ImGui::Text(component.geometry.c_str());
				}
			}

			if (ImGui::selectedEntity.HasAll< DynamicMeshComponent >())
			{
				if (ImGui::CollapsingHeader("DynamicMesh"))
				{
					auto& component = ImGui::selectedEntity.GetComponent< DynamicMeshComponent >();

					if (ImGui::Button("Texture")) ImGui::contentBrowserSetup = eContentButton_Texture;
					ImGui::SameLine(); ImGui::Text(component.texture.c_str());
					if (ImGui::Button("Geometry")) ImGui::contentBrowserSetup = eContentButton_Geometry;
					ImGui::SameLine(); ImGui::Text(component.geometry.c_str());
				}
			}

			if (ImGui::Button("Add Component"))
				ImGui::OpenPopup("AddComponentPopup");

			if (ImGui::BeginPopup("AddComponentPopup")) 
			{
				if (!ImGui::selectedEntity.HasAny< StaticMeshComponent, DynamicMeshComponent >())
				{
					if (ImGui::MenuItem("StaticMesh"))
					{
						ImGui::selectedEntity.AttachComponent< StaticMeshComponent >();
					}

					if (ImGui::MenuItem("DynamicMesh"))
					{
						ImGui::selectedEntity.AttachComponent< DynamicMeshComponent >();
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
	if (ImGui::contentBrowserSetup)
	{
		ImGui::Begin("Content Browser");
		{
			if (m_currentDirectory != fs::path(ASSET_PATH))
			{
				if (ImGui::Button("<"))
				{
					m_currentDirectory = m_currentDirectory.parent_path();
				}
			}

			for (auto& entry : std::filesystem::directory_iterator(m_currentDirectory))
			{
				const auto& path = entry.path();
				auto relativePath = fs::relative(entry.path(), ASSET_PATH);
				auto filenameStr = relativePath.filename().string();
				if (entry.is_directory())
				{
					if (ImGui::Button(filenameStr.c_str()))
					{
						m_currentDirectory /= path.filename();
					}
				}
				else
				{
					std::string extensionStr = path.extension().string();

					switch(ImGui::contentBrowserSetup)
					{
					case eContentButton_Texture:
						if (extensionStr == ".png" || extensionStr == ".jpg")
						{
							if (ImGui::Selectable(filenameStr.c_str())) 
							{
								if (ImGui::selectedEntity.HasAll< StaticMeshComponent >())
								{
									auto& component = ImGui::selectedEntity.GetComponent< StaticMeshComponent >();
									component.texture = path.string();
								}
								else if (ImGui::selectedEntity.HasAll< DynamicMeshComponent >())
								{
									auto& component = ImGui::selectedEntity.GetComponent< DynamicMeshComponent >();
									component.texture = path.string();
								}

								ImGui::contentBrowserSetup = 0;
							}
						}
						break;
					case eContentButton_Geometry:
						if (extensionStr == ".fbx" || extensionStr == ".obj")
						{
							if (ImGui::Selectable(filenameStr.c_str()))
							{
								if (ImGui::selectedEntity.HasAll< StaticMeshComponent >())
								{
									auto& component = ImGui::selectedEntity.GetComponent< StaticMeshComponent >();
									component.geometry = path.string();
								}
								else if (ImGui::selectedEntity.HasAll< DynamicMeshComponent >())
								{
									auto& component = ImGui::selectedEntity.GetComponent< DynamicMeshComponent >();
									component.geometry = path.string();
								}

								ImGui::contentBrowserSetup = 0;
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
	if (ImGui::selectedEntity == entity) 
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
		ImGui::selectedEntity = entity;

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
			RELEASE(m_pRendererBackend);
			m_eBackendAPI = eRendererAPI::Vulkan;

			if (!LoadRenderer(m_eBackendAPI, m_pWindow, ImGui::GetCurrentContext(), &m_pRendererBackend))
				throw std::runtime_error("Failed to load backend!");
		}
	}
	else if (glfwGetKey(m_pWindow->Handle(), GLFW_KEY_LEFT_SHIFT) && glfwGetKey(m_pWindow->Handle(), GLFW_KEY_D))
	{
		if (m_eBackendAPI != eRendererAPI::D3D12)
		{
			RELEASE(m_pRendererBackend);
			m_eBackendAPI = eRendererAPI::D3D12;

			if (!LoadRenderer(m_eBackendAPI, m_pWindow, ImGui::GetCurrentContext(), &m_pRendererBackend))
				throw std::runtime_error("Failed to load backend!");
		}
	}
}

} // namespace baamboo
