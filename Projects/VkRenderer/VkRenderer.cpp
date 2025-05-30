#include "RendererPch.h"
#include "VkRenderer.h"
#include "RenderDevice/VkSwapChain.h"
#include "RenderDevice/VkResourceManager.h"
#include "RenderDevice/VkCommandQueue.h"
#include "RenderDevice/VkCommandBuffer.h"
#include "RenderDevice/VkDescriptorSet.h"
#include "RenderResource/VkTexture.h"
#include "RenderResource/VkSceneResource.h"
#include "RenderModule/VkForwardModule.h"
#include "RenderModule/VkImGuiModule.h"

#include <Scene/SceneRenderView.h>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_vulkan.h>

namespace vk
{

Renderer::Renderer(baamboo::Window* pWindow, ImGuiContext* pImGuiContext)
{
	assert(pWindow);

	m_pRenderContext = new RenderContext();
	m_pSwapChain = new SwapChain(*m_pRenderContext, *pWindow);

	m_pRenderModules.push_back(new ForwardModule(*m_pRenderContext));
	m_pRenderModules.push_back(new ImGuiModule(*m_pRenderContext, *m_pSwapChain, pImGuiContext));

	printf("VkRenderer constructed!\n");

	CreateDefaultResources();
}

Renderer::~Renderer()
{
	m_pRenderContext->GraphicsQueue().Flush();
	m_pRenderContext->ComputeQueue().Flush();
	if (m_pRenderContext->TransferQueue()) 
		m_pRenderContext->TransferQueue()->Flush();
	vkDeviceWaitIdle(m_pRenderContext->vkDevice());

	for (auto pModule : m_pRenderModules)
		RELEASE(pModule);

	RELEASE(m_pSwapChain);
	RELEASE(m_pRenderContext);

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

	auto& cmdBuffer = BeginFrame();
	{
		CameraData camera = {};
		camera.mView = renderView.camera.mView;
		camera.mProj = ApplyVulkanNDC(renderView.camera.mProj);
		camera.mViewProj = camera.mProj * camera.mView;
		camera.position = renderView.camera.pos;
		cmdBuffer.SetGraphicsDynamicUniformBuffer(0, camera);

		g_FrameData.camera = std::move(camera);
		
		for (auto pModule : m_pRenderModules)
			pModule->Apply(cmdBuffer);
	}
	EndFrame(cmdBuffer);
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

	VK_CHECK(vkDeviceWaitIdle(m_pRenderContext->vkDevice()));
	m_pSwapChain->ResizeViewport();

	for (auto pModule : m_pRenderModules)
		pModule->Resize(width, height);
}

void Renderer::CreateDefaultResources()
{
	// TODO
}

CommandBuffer& Renderer::BeginFrame()
{
	auto& cmdBuffer = m_pRenderContext->GraphicsQueue().Allocate();
	m_pSwapChain->AcquireImageIndex(cmdBuffer.vkPresentCompleteSemaphore());

	return cmdBuffer;
}

void Renderer::EndFrame(CommandBuffer& cmdBuffer)
{
	auto& rm = m_pRenderContext->GetResourceManager();
	auto backBuffer = m_pSwapChain->GetImageToPresent();
	
	//cmdBuffer.ClearTexture(rm.Get(backBuffer), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_HOST_BIT, VK_PIPELINE_STAGE_2_CLEAR_BIT);
	cmdBuffer.CopyTexture(rm.Get(backBuffer), rm.Get(g_FrameData.color));
	cmdBuffer.TransitionImageLayout(
		rm.Get(backBuffer),
		VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, 
		//VK_PIPELINE_STAGE_2_TRANSFER_BIT,
		VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
	cmdBuffer.Close();

	m_pRenderContext->GraphicsQueue().ExecuteCommandBuffer(cmdBuffer);
	m_pSwapChain->Present(cmdBuffer.vkRenderCompleteSemaphore());
}

} // namespace vk
