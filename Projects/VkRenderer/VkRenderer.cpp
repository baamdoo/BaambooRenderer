#include "RendererPch.h"
#include "VkRenderer.h"
#include "RenderDevice/VkSwapChain.h"
#include "RenderDevice/VkResourceManager.h"
#include "RenderDevice/VkCommandContext.h"
#include "RenderDevice/VkDescriptorSet.h"
#include "RenderResource/VkTexture.h"
#include "RenderResource/VkSceneResource.h"
#include "RenderModule/VkForwardModule.h"
#include "RenderModule/VkImGuiModule.h"
#include "SceneRenderView.h"

#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_vulkan.h>

namespace vk
{

Renderer::Renderer(baamboo::Window* pWindow, ImGuiContext* pImGuiContext)
{
	assert(pWindow);

	m_pRenderDevice = new RenderDevice();
	m_pSwapChain    = new SwapChain(*m_pRenderDevice, *pWindow);

	g_FrameData.pSceneResource = new SceneResource(*m_pRenderDevice);

	m_pRenderModules.push_back(new ForwardModule(*m_pRenderDevice));
	m_pRenderModules.push_back(new ImGuiModule(*m_pRenderDevice, *m_pSwapChain, pImGuiContext));

	printf("VkRenderer constructed!\n");
}

Renderer::~Renderer()
{
	m_pRenderDevice->Flush();
	vkDeviceWaitIdle(m_pRenderDevice->vkDevice());

	RELEASE(g_FrameData.pSceneResource);
	for (auto pModule : m_pRenderModules)
		RELEASE(pModule);

	RELEASE(m_pSwapChain);
	RELEASE(m_pRenderDevice);

	printf("VkRenderer destructed!\n");
}

void Renderer::Render(SceneRenderView&& renderView)
{
	if (!m_pSwapChain->IsRenderable())
	{
		return;
	}

	assert(g_FrameData.pSceneResource);
	g_FrameData.pSceneResource->UpdateSceneResources(renderView);

	auto ApplyVulkanNDC = [](const mat4& mProj_) 
		{
			mat4 mProj = mProj_;
			mProj[1][1] *= -1.0f;
			return mProj;
		};

	auto& context = BeginFrame();
	{
		CameraData camera = {};
		camera.mView     = renderView.camera.mView;
		camera.mProj     = ApplyVulkanNDC(renderView.camera.mProj);
		camera.mViewProj = camera.mProj * camera.mView;
		camera.position  = renderView.camera.pos;

		g_FrameData.camera = std::move(camera);
		
		for (auto pModule : m_pRenderModules)
			pModule->Apply(context);
	}
	EndFrame(context);
}

void Renderer::NewFrame()
{
	ImGui_ImplVulkan_NewFrame();
}

void Renderer::SetRendererType(eRendererType type)
{
	m_Type = type;
}

void Renderer::OnWindowResized(i32 width, i32 height)
{
	if (width == 0 || height == 0)
		return;

	VK_CHECK(vkDeviceWaitIdle(m_pRenderDevice->vkDevice()));
	m_pSwapChain->ResizeViewport();

	for (auto pModule : m_pRenderModules)
		pModule->Resize(width, height);
}

CommandContext& Renderer::BeginFrame()
{
	auto& context = m_pRenderDevice->BeginCommand(eCommandType::Graphics);
	m_pSwapChain->AcquireImageIndex(context.vkPresentCompleteSemaphore());

	return context;
}

void Renderer::EndFrame(CommandContext& context)
{
	auto pBackBuffer = m_pSwapChain->GetImageToPresent();
	
	if (g_FrameData.pColor.valid())
	{
		context.BlitTexture(pBackBuffer, g_FrameData.pColor.lock());
	}
	context.TransitionImageLayout(
		pBackBuffer,
		VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, 
		//VK_PIPELINE_STAGE_2_TRANSFER_BIT,
		VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
	context.Close();

	context.Execute();
	m_pSwapChain->Present(context.vkRenderCompleteSemaphore());
}

} // namespace vk
