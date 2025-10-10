#include "RendererPch.h"
#include "VkRenderer.h"
#include "RenderDevice/VkSwapChain.h"
#include "RenderDevice/VkFrameManager.h"
#include "RenderDevice/VkResourceManager.h"
#include "RenderDevice/VkCommandContext.h"
#include "RenderDevice/VkDescriptorSet.h"
#include "RenderResource/VkTexture.h"
#include "RenderResource/VkSceneResource.h"
#include "RenderModule/VkImGuiModule.h"
#include "SceneRenderView.h"
#include "Utils/Math.hpp"

#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_vulkan.h>

namespace vk
{

VkRenderer::VkRenderer(baamboo::Window* pWindow, ImGuiContext* pImGuiContext)
{
	assert(pWindow);

	m_pRenderDevice = new VkRenderDevice();
	m_pSwapChain    = new SwapChain(*m_pRenderDevice, *pWindow);
	m_pFrameManager = new FrameManager(*m_pRenderDevice, *m_pSwapChain);

	m_ImGuiModule = MakeBox< ImGuiModule >(*m_pRenderDevice, *m_pSwapChain, pImGuiContext);

	printf("VkRenderer constructed!\n");
}
VkRenderer::~VkRenderer()
{
	WaitIdle();

	// explicit release due to release dependency
	m_ImGuiModule.reset();

	RELEASE(m_pFrameManager);
	RELEASE(m_pSwapChain);
	RELEASE(m_pRenderDevice);

	printf("VkRenderer destructed!\n");
}

Arc< render::CommandContext > VkRenderer::BeginFrame()
{
	if (!m_pSwapChain->IsRenderable())
	{
		return nullptr;
	}

	auto context = m_pFrameManager->BeginFrame();
	return context.rhiCommandContext;
}

void VkRenderer::EndFrame(Arc< render::CommandContext >&& context, Arc< render::Texture > pScene, bool bDrawUI)
{
	auto rhiContext = StaticCast<VkCommandContext>(context);
	assert(rhiContext);

	auto pColor = StaticCast<VulkanTexture>(pScene);
	assert(pColor);
	if (bDrawUI)
	{
		m_ImGuiModule->Apply(*rhiContext, pColor);
	}

	auto pBackBuffer = m_pSwapChain->GetImageToPresent();
	rhiContext->BlitTexture(pBackBuffer, pColor);

	rhiContext->TransitionImageLayout(
		pBackBuffer,
		VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
		VK_IMAGE_ASPECT_COLOR_BIT);
	rhiContext->Close();

	m_pFrameManager->EndFrame(std::move(rhiContext));
}

void VkRenderer::NewFrame()
{
	ImGui_ImplVulkan_NewFrame();
}

void VkRenderer::WaitIdle()
{
	m_pRenderDevice->Flush();
	vkDeviceWaitIdle(m_pRenderDevice->vkDevice());
}

void VkRenderer::Resize(i32 width, i32 height)
{
	if (width == 0 || height == 0)
		return;

	if (m_pRenderDevice->WindowWidth() == static_cast<u32>(width) && m_pRenderDevice->WindowHeight() == static_cast<u32>(height))
		return;

	VK_CHECK(vkDeviceWaitIdle(m_pRenderDevice->vkDevice()));
	m_pRenderDevice->SetWindowWidth(width);
	m_pRenderDevice->SetWindowHeight(height);

	m_pSwapChain->ResizeViewport();
}

} // namespace vk
