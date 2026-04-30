#include "RendererPch.h"
#include "VkShader.h"
#include "VkSceneResource.h"

#include "RenderDevice/VkDescriptorSet.h"
#include "RenderDevice/VkDescriptorPool.h"
#include "RenderDevice/VkResourceManager.h"
#include "RenderDevice/VkBufferAllocator.h"
#include "RenderDevice/VkRenderPipeline.h"
#include "RenderDevice/VkCommandContext.h"

#include "Timer.h"
#include "SceneRenderView.h"

#include "Utils/Math.hpp"

namespace vk
{

enum
{
	eCommonSetBindingIndex_Camera  = 0,

	eCommonSetBindingIndex_Vertex          = 1,

	eCommonSetBindingIndex_Meshlet         = 2,
	eCommonSetBindingIndex_MeshletVertex   = 3,
	eCommonSetBindingIndex_MeshletTriangle = 4,

	eCommonSetBindingIndex_MeshData = 5,
	eCommonSetBindingIndex_Instance = 6,
	eCommonSetBindingIndex_Cull     = 7,

	eCommonSetBindingIndex_Transform     = 8,
	eCommonSetBindingIndex_Material      = 9,
	eCommonSetBindingIndex_SceneTextures = 100,

	eCommonSetBindingIndex_Light       = 10,
	eCommonSetBindingIndex_Environment = 11,
};

static VulkanComputePipeline* s_CombineTexturesPipeline = nullptr;
Arc< VulkanTexture > CombineTextures(VkRenderDevice& rd, const char* name, Arc< VulkanTexture > pTextureR, Arc< VulkanTexture > pTextureG, Arc< VulkanTexture > pTextureB, Arc< VulkanSampler > pSampler)
{
	using namespace render;

	u32 width 
		= std::max({ pTextureR->Desc().extent.width, pTextureG->Desc().extent.width, pTextureB->Desc().extent.width });
	u32 height 
		= std::max({ pTextureR->Desc().extent.height, pTextureG->Desc().extent.height, pTextureB->Desc().extent.height });
	auto pCombinedTexture =
		VulkanTexture::Create(
			rd,
			name,
			{
				.resolution = { width, height, 1 },
				.imageUsage = eTextureUsage_Sample | eTextureUsage_Storage
			});

	auto pContext = rd.BeginCommand(eCommandType::Compute, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, true);
	if (pContext)
	{
		pContext->SetRenderPipeline(s_CombineTexturesPipeline);

		VkImageSubresourceRange subresourceRange = {};
		subresourceRange.aspectMask   = VK_IMAGE_ASPECT_COLOR_BIT;
		subresourceRange.baseMipLevel = 0;
		subresourceRange.levelCount   = pCombinedTexture->Desc().mipLevels;
		subresourceRange.layerCount   = pCombinedTexture->Desc().arrayLayers;
		pContext->TransitionImageLayout(pTextureR, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, subresourceRange);
		pContext->TransitionImageLayout(pTextureG, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, subresourceRange);
		pContext->TransitionImageLayout(pTextureB, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, subresourceRange);
		pContext->TransitionImageLayout(pCombinedTexture, VK_IMAGE_LAYOUT_GENERAL, subresourceRange);
		pContext->PushDescriptor(
			1, 0, 
			{ pSampler->vkSampler(), pTextureR->vkView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
		pContext->PushDescriptor(
			1, 1,
			{ pSampler->vkSampler(), pTextureG->vkView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
		pContext->PushDescriptor(
			1, 2,
			{ pSampler->vkSampler(), pTextureB->vkView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
		pContext->PushDescriptor(
			1, 3,
			{ pSampler->vkSampler(), pCombinedTexture->vkView(), VK_IMAGE_LAYOUT_GENERAL },
			VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);

		pContext->Dispatch2D< 16, 16 >(width, height);

		pContext->TransitionImageLayout(pCombinedTexture, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, subresourceRange);
		pContext->Close();

		rd.ExecuteCommand(pContext);
	}

	return pCombinedTexture;
}


void VkSceneResource::PerFrameData::Reset()
{
	bInitialized = true;

	if (pMeshDataAllocator)
		pMeshDataAllocator->Reset();
	if (pInstanceAllocator)

		pInstanceAllocator->Reset();
	if (pTransformAllocator) 
		pTransformAllocator->Reset();
	if (pMaterialAllocator) 
		pMaterialAllocator->Reset();
	if (pLightAllocator) 
		pLightAllocator->Reset();
	/*if (pIndirectCommandAllocator) 
		pIndirectCommandAllocator->Reset();*/
}


VkSceneResource::VkSceneResource(VkRenderDevice& rd)
	: m_RenderDevice(rd)
{
	// **
	// scene buffers
	// **

	m_pVertexAllocator          = MakeBox< StaticBufferAllocator >(m_RenderDevice, sizeof(Vertex) * _KB(1LL), VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT);
	m_pIndexAllocator           = MakeBox< StaticBufferAllocator >(m_RenderDevice, sizeof(Index) * 3 * _KB(1LL), VK_BUFFER_USAGE_2_INDEX_BUFFER_BIT);
	m_pMeshletAllocator         = MakeBox< StaticBufferAllocator >(m_RenderDevice, sizeof(Meshlet) * _KB(1LL), VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT);
	m_pMeshletVertexAllocator   = MakeBox< StaticBufferAllocator >(m_RenderDevice, sizeof(u32) * _KB(1LL), VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT);
	m_pMeshletTriangleAllocator = MakeBox< StaticBufferAllocator >(m_RenderDevice, sizeof(u32) * _KB(1LL), VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT);

	for (auto& frameData : m_FrameData)
	{
		frameData.pMeshDataAllocator        = MakeBox< StaticBufferAllocator >(m_RenderDevice, sizeof(MeshData) * _KB(1LL), VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_2_INDIRECT_BUFFER_BIT);
		frameData.pInstanceAllocator        = MakeBox< StaticBufferAllocator >(m_RenderDevice, sizeof(InstanceData) * MAX_ENTITY_COUNT, VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_2_INDIRECT_BUFFER_BIT);
		//frameData.pIndirectCommandAllocator = MakeBox< StaticBufferAllocator >(m_RenderDevice, sizeof(IndirectCommandData) * MAX_ENTITY_COUNT, VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_2_INDIRECT_BUFFER_BIT);

		frameData.pTransformAllocator = MakeBox< StaticBufferAllocator >(m_RenderDevice, sizeof(TransformData) * MAX_ENTITY_COUNT, VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT);
		frameData.pMaterialAllocator  = MakeBox< StaticBufferAllocator >(m_RenderDevice, sizeof(MaterialData) * MAX_ENTITY_COUNT, VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT);
		frameData.pLightAllocator     = MakeBox< StaticBufferAllocator >(m_RenderDevice, sizeof(LightData), VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT);

		frameData.pCameraBuffer           = VulkanUniformBuffer::Create(m_RenderDevice, "CameraBuffer", sizeof(CameraData));
		frameData.pCullBuffer             = VulkanUniformBuffer::Create(m_RenderDevice, "CullBuffer", sizeof(CullData));
		frameData.pSceneEnvironmentBuffer = VulkanUniformBuffer::Create(m_RenderDevice, "SceneEnvironmentBuffer", sizeof(SceneEnvironmentData));
	}


	// **
	// scene descriptor pool
	// **
	std::vector< VkDescriptorSetLayoutBinding > bindings =
	{
		{ eCommonSetBindingIndex_Camera, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_ALL, VK_NULL_HANDLE },
		{ eCommonSetBindingIndex_Vertex, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_MESH_BIT_EXT, VK_NULL_HANDLE },
		{ eCommonSetBindingIndex_Meshlet, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_TASK_BIT_EXT | VK_SHADER_STAGE_MESH_BIT_EXT, VK_NULL_HANDLE },
		{ eCommonSetBindingIndex_MeshletVertex, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_TASK_BIT_EXT | VK_SHADER_STAGE_MESH_BIT_EXT, VK_NULL_HANDLE },
		{ eCommonSetBindingIndex_MeshletTriangle, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_TASK_BIT_EXT | VK_SHADER_STAGE_MESH_BIT_EXT, VK_NULL_HANDLE },
		{ eCommonSetBindingIndex_MeshData, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_TASK_BIT_EXT | VK_SHADER_STAGE_MESH_BIT_EXT, VK_NULL_HANDLE },
		{ eCommonSetBindingIndex_Instance, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_TASK_BIT_EXT | VK_SHADER_STAGE_MESH_BIT_EXT, VK_NULL_HANDLE },
		{ eCommonSetBindingIndex_Cull, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_TASK_BIT_EXT, VK_NULL_HANDLE },
		{ eCommonSetBindingIndex_Transform, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_TASK_BIT_EXT | VK_SHADER_STAGE_MESH_BIT_EXT, VK_NULL_HANDLE },
		{ eCommonSetBindingIndex_Material, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, VK_NULL_HANDLE },
		{ eCommonSetBindingIndex_Light, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, VK_NULL_HANDLE },
		{ eCommonSetBindingIndex_Environment, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, VK_NULL_HANDLE },
		{ eCommonSetBindingIndex_SceneTextures, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAX_BINDLESS_DESCRIPTOR_RESOURCE_COUNT, VK_SHADER_STAGE_FRAGMENT_BIT, VK_NULL_HANDLE },
	};

	std::unordered_map< VkDescriptorType, VkDescriptorPoolSize > poolSizeMap;
	for (const auto& binding : bindings)
	{
		auto& poolSize = poolSizeMap[binding.descriptorType];
		poolSize.type             = binding.descriptorType;
		poolSize.descriptorCount += binding.descriptorCount;
	}

	std::vector< VkDescriptorPoolSize > poolSizes;
	std::transform(
		std::move_iterator(poolSizeMap.begin()),
		std::move_iterator(poolSizeMap.end()),
		std::back_inserter(poolSizes),
		[](std::pair< VkDescriptorType, VkDescriptorPoolSize >&& entry) { return std::move(entry.second); });
	m_pDescriptorPool = new DescriptorPool(m_RenderDevice, std::move(poolSizes), 1, VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT);

	std::vector < VkDescriptorBindingFlags > flags =
	{
		VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT_EXT,
		VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT_EXT,
		VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT_EXT,
		VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT_EXT,
		VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT_EXT,
		VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT_EXT,
		VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT_EXT,
		VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT_EXT,
		VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT_EXT,
		VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT_EXT,
		VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT_EXT,
		VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT_EXT,
		VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT_EXT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT,
	};

	VkDescriptorSetLayoutBindingFlagsCreateInfoEXT bindingFlagsInfo = {};
	bindingFlagsInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO_EXT;
	bindingFlagsInfo.bindingCount  = static_cast<u32>(flags.size());
	bindingFlagsInfo.pBindingFlags = flags.data();

	VkDescriptorSetLayoutCreateInfo layoutInfo = {};
	layoutInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.pNext        = &bindingFlagsInfo;
	layoutInfo.flags        = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT_EXT;
	layoutInfo.bindingCount = static_cast<u32>(bindings.size());
	layoutInfo.pBindings    = bindings.data();
	VK_CHECK(vkCreateDescriptorSetLayout(m_RenderDevice.vkDevice(), &layoutInfo, nullptr, &m_vkSetLayout));


	// **
	// Global pipeline layout
	// **
	VkPipelineLayoutCreateInfo globalPipelineLayoutInfo = {};
	globalPipelineLayoutInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	globalPipelineLayoutInfo.setLayoutCount         = 1;
	globalPipelineLayoutInfo.pSetLayouts            = &m_vkSetLayout;
	globalPipelineLayoutInfo.pushConstantRangeCount = 0;
	globalPipelineLayoutInfo.pPushConstantRanges    = nullptr;
	vkCreatePipelineLayout(m_RenderDevice.vkDevice(), &globalPipelineLayoutInfo, nullptr, &m_vkGlobalPipelineLayout);


	// **
	// default sampler for scene textures
	// **
	m_pDefaultSampler = VulkanSampler::Create(m_RenderDevice, "DefaultSampler", {});
}

VkSceneResource::~VkSceneResource()
{
	imageInfos.clear();

	vkDestroyPipelineLayout(m_RenderDevice.vkDevice(), m_vkGlobalPipelineLayout, nullptr);
	vkDestroyDescriptorSetLayout(m_RenderDevice.vkDevice(), m_vkSetLayout, nullptr);
	RELEASE(m_pDescriptorPool);

	RELEASE(s_CombineTexturesPipeline);
}

void VkSceneResource::UpdateCameraAndEnvironment(const SceneRenderView& sceneView, VkCommandContext& ctx)
{
	auto ApplyJittering = [viewport = sceneView.viewport](const mat4& m_, float2 jitter)
		{
			mat4 m = m_;
			m[2][0] += (jitter.x * 2.0f - 1.0f) / viewport.x;
			m[2][1] += (jitter.y * 2.0f - 1.0f) / viewport.y;
			return m;
		};

	CameraData camera = {};
	camera.mView = sceneView.camera.mView;
	camera.mProj = ApplyRhiNDC((sceneView.postProcess.effectBits & (1 << ePostProcess::AntiAliasing) ?
		ApplyJittering(sceneView.camera.mProj, baamboo::math::GetHaltonSequence((u32)sceneView.frame)) : sceneView.camera.mProj), eRendererAPI::Vulkan);
	camera.mViewProj               = camera.mProj * camera.mView;
	camera.mViewProjInv            = glm::inverse(camera.mViewProj);
	camera.mViewProjUnjittered     = ApplyRhiNDC(sceneView.camera.mProj, eRendererAPI::Vulkan) * camera.mView;
	camera.mViewProjUnjitteredPrev =
		m_CameraCache.mViewProjUnjittered == glm::identity< mat4 >() ? camera.mViewProjUnjittered : m_CameraCache.mViewProjUnjittered;
	camera.position = sceneView.camera.pos;
	camera.zNear    = sceneView.camera.zNear;
	camera.zFar     = sceneView.camera.zFar;

	m_CameraCache = std::move(camera);
	memcpy(m_FrameData[m_ContextIndex].pCameraBuffer->MappedMemory(), &m_CameraCache, sizeof(CameraData));

	mat4 mViewProjectionT = glm::transpose(m_CameraCache.mViewProjUnjittered);

	m_CullData = {};
	m_CullData.frustum[0] = baamboo::math::NormalizePlane(mViewProjectionT[3] + mViewProjectionT[0]); // w + x < 0
	m_CullData.frustum[1] = baamboo::math::NormalizePlane(mViewProjectionT[3] - mViewProjectionT[0]); // w - x < 0
	m_CullData.frustum[2] = baamboo::math::NormalizePlane(mViewProjectionT[3] + mViewProjectionT[1]); // w + y < 0
	m_CullData.frustum[3] = baamboo::math::NormalizePlane(mViewProjectionT[3] - mViewProjectionT[1]); // w - y < 0
	m_CullData.frustum[4] = baamboo::math::NormalizePlane(mViewProjectionT[3] - mViewProjectionT[2]); // w - z < 0 (reversed-z)
	m_CullData.frustum[5] = float4();                                                                 // z < 0 (reversed-z, infinite far plane)

	m_CullData.sseThresholdPx = sceneView.sseThresholdPx;
	m_CullData.viewportHeight = sceneView.viewport.y;
	memcpy(m_FrameData[m_ContextIndex].pCullBuffer->MappedMemory(), &m_CullData, sizeof(CullData));

	SceneEnvironmentData sceneEnvironmentData =
	{
		.atmosphere = sceneView.atmosphere.data,
		.cloud      = sceneView.cloud.data
	};
	memcpy(m_FrameData[m_ContextIndex].pSceneEnvironmentBuffer->MappedMemory(), &sceneEnvironmentData, sizeof(SceneEnvironmentData));

	// Re-stage descriptors (imageInfos retains last full-update's textures; per-frame allocators retain their data)
	u32 variableDescCounts[] = { static_cast<u32>(imageInfos.size()) };
	auto& descriptorSet = m_pDescriptorPool->AllocateSet(m_vkSetLayout, variableDescCounts);
	descriptorSet.StageDescriptor({ m_FrameData[m_ContextIndex].pCameraBuffer->vkBuffer(), 0, m_FrameData[m_ContextIndex].pCameraBuffer->SizeInBytes() }, eCommonSetBindingIndex_Camera, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
	descriptorSet.StageDescriptor({ m_FrameData[m_ContextIndex].pCullBuffer->vkBuffer(), 0, m_FrameData[m_ContextIndex].pCullBuffer->SizeInBytes() }, eCommonSetBindingIndex_Cull, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
	descriptorSet.StageDescriptor({ m_FrameData[m_ContextIndex].pSceneEnvironmentBuffer->vkBuffer(), 0, m_FrameData[m_ContextIndex].pSceneEnvironmentBuffer->SizeInBytes() }, eCommonSetBindingIndex_Environment, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);

	descriptorSet.StageDescriptors(imageInfos, eCommonSetBindingIndex_SceneTextures, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
	descriptorSet.StageDescriptor(m_pVertexAllocator->GetDescriptorInfo(), eCommonSetBindingIndex_Vertex, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
	descriptorSet.StageDescriptor(m_pMeshletAllocator->GetDescriptorInfo(), eCommonSetBindingIndex_Meshlet, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
	descriptorSet.StageDescriptor(m_pMeshletVertexAllocator->GetDescriptorInfo(), eCommonSetBindingIndex_MeshletVertex, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
	descriptorSet.StageDescriptor(m_pMeshletTriangleAllocator->GetDescriptorInfo(), eCommonSetBindingIndex_MeshletTriangle, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);

	descriptorSet.StageDescriptor(m_FrameData[m_ContextIndex].pMeshDataAllocator->GetDescriptorInfo(), eCommonSetBindingIndex_MeshData, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
	descriptorSet.StageDescriptor(m_FrameData[m_ContextIndex].pInstanceAllocator->GetDescriptorInfo(), eCommonSetBindingIndex_Instance, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
	descriptorSet.StageDescriptor(m_FrameData[m_ContextIndex].pTransformAllocator->GetDescriptorInfo(), eCommonSetBindingIndex_Transform, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);

	descriptorSet.StageDescriptor(m_FrameData[m_ContextIndex].pMaterialAllocator->GetDescriptorInfo(), eCommonSetBindingIndex_Material, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
	descriptorSet.StageDescriptor(m_FrameData[m_ContextIndex].pLightAllocator->GetDescriptorInfo(), eCommonSetBindingIndex_Light, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
}

void VkSceneResource::UpdateSceneResources(const SceneRenderView& sceneView, render::CommandContext& context)
{
	auto& rm  = static_cast<VkResourceManager&>(m_RenderDevice.GetResourceManager());
	auto& ctx = static_cast<VkCommandContext&>(context);
	if (sceneView.pEntityDirtyMarks != nullptr)
	{
		for (auto& frameData : m_FrameData)
		{
			frameData.bInitialized = false;
		}
	}
	else if (m_FrameData[m_ContextIndex].bInitialized)
	{
		UpdateCameraAndEnvironment(sceneView, ctx);
		return;
	}

	ResetFrameBuffers();

	std::vector< TransformData > transforms;
	transforms.reserve(sceneView.transforms.size());
	for (auto& transformView : sceneView.transforms)
	{
		TransformData transform = {};
		transform.mLocalToWorld = transformView.mWorld;
		transform.mWorldToLocal = transformView.mWorldInverse;
		transforms.push_back(transform);
	}
	UpdateFrameBuffer(ctx, transforms.data(), (u32)transforms.size(), sizeof(TransformData), *m_FrameData[m_ContextIndex].pTransformAllocator, VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT | VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);

	imageInfos.clear();
	imageInfos.push_back({ m_pDefaultSampler->vkSampler(), StaticCast<VulkanTexture>(rm.GetFlatWhiteTexture())->vkView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
	imageInfos.push_back({ m_pDefaultSampler->vkSampler(), StaticCast<VulkanTexture>(rm.GetFlatBlackTexture())->vkView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
	imageInfos.push_back({ m_pDefaultSampler->vkSampler(), StaticCast<VulkanTexture>(rm.GetFlatGrayTexture())->vkView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });

	std::vector< MaterialData > materials;
	materials.reserve(sceneView.materials.size());
	std::unordered_map< VulkanTexture*, u32 > srvIndexCache;
	for (auto& materialView : sceneView.materials)
	{
		MaterialData material  = {};
		material.tint          = materialView.tint;
		material.roughness     = materialView.roughness;
		material.metallic      = materialView.metallic;
		material.ior           = materialView.ior;
		material.emissivePower = materialView.emissivePower;

		material.albedoID = INVALID_INDEX;
		if (!materialView.albedoTex.empty())
		{
			auto pMaterialTex = GetOrLoadTexture(materialView.id, materialView.albedoTex);
			if (srvIndexCache.contains(pMaterialTex.get()))
			{
				material.albedoID = srvIndexCache[pMaterialTex.get()];
			}
			else
			{
				imageInfos.push_back({ m_pDefaultSampler->vkSampler(), pMaterialTex->vkView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
				material.albedoID = (u32)imageInfos.size() - 1;
				srvIndexCache.emplace(pMaterialTex.get(), material.albedoID);
			}
		}

		material.normalID = INVALID_INDEX;
		if (!materialView.normalTex.empty())
		{
			auto pMaterialTex = GetOrLoadTexture(materialView.id, materialView.normalTex);
			if (srvIndexCache.contains(pMaterialTex.get()))
			{
				material.normalID = srvIndexCache[pMaterialTex.get()];
			}
			else
			{
				imageInfos.push_back({ m_pDefaultSampler->vkSampler(), pMaterialTex->vkView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
				material.normalID = (u32)imageInfos.size() - 1;
				srvIndexCache.emplace(pMaterialTex.get(), material.normalID);
			}
		}

		// combine orm
		std::string aoStr        = materialView.aoTex;
		std::string roughnessStr = materialView.roughnessTex;
		std::string metallicStr  = materialView.metallicTex;
		std::string ormStr       = aoStr + roughnessStr + metallicStr;

		material.metallicRoughnessAoID = INVALID_INDEX;
		auto pORM = GetTexture(ormStr);
		if (!pORM)
		{
			if (s_CombineTexturesPipeline == nullptr)
			{
				s_CombineTexturesPipeline = new VulkanComputePipeline(m_RenderDevice, "CombineTextures");
				Arc< VulkanShader > pCS
					= VulkanShader::Create(m_RenderDevice, "CombineTextures", { .stage = render::eShaderStage::Compute, .filename = "CombineTextures" });
				s_CombineTexturesPipeline->SetComputeShader(std::move(pCS)).Build();
			}

			Arc< VulkanTexture > pCombiningTextures[3] = {};
			if (!materialView.aoTex.empty())
				pCombiningTextures[0] = GetOrLoadTexture(materialView.id, materialView.aoTex);
			else
				pCombiningTextures[0] = StaticCast<VulkanTexture>(rm.GetFlatWhiteTexture());

			if (!materialView.roughnessTex.empty())
				pCombiningTextures[1] = GetOrLoadTexture(materialView.id, materialView.roughnessTex);
			else
				pCombiningTextures[1] = StaticCast<VulkanTexture>(rm.GetFlatWhiteTexture());

			if (!materialView.metallicTex.empty())
				pCombiningTextures[2] = GetOrLoadTexture(materialView.id, materialView.metallicTex);
			else
				pCombiningTextures[2] = StaticCast<VulkanTexture>(rm.GetFlatWhiteTexture());

			pORM = CombineTextures(m_RenderDevice, "ORM", pCombiningTextures[0], pCombiningTextures[1], pCombiningTextures[2], m_pDefaultSampler);
			m_TextureCache.emplace(ormStr, pORM);
		}

		if (srvIndexCache.contains(pORM.get()))
		{
			material.metallicRoughnessAoID = srvIndexCache[pORM.get()];
		}
		else
		{
			imageInfos.push_back({ m_pDefaultSampler->vkSampler(), pORM->vkView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
			material.metallicRoughnessAoID = (u32)imageInfos.size() - 1;
			srvIndexCache.emplace(pORM.get(), material.metallicRoughnessAoID);
		}

		material.emissiveID = INVALID_INDEX;
		if (!materialView.emissionTex.empty())
		{
			auto pMaterialTex = GetOrLoadTexture(materialView.id, materialView.emissionTex);
			if (srvIndexCache.contains(pMaterialTex.get()))
			{
				material.emissiveID = srvIndexCache[pMaterialTex.get()];
			}
			else
			{
				imageInfos.push_back({ m_pDefaultSampler->vkSampler(), pMaterialTex->vkView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
				material.emissiveID = (u32)imageInfos.size() - 1;
				srvIndexCache.emplace(pMaterialTex.get(), material.emissiveID);
			}
		}

		material.clearcoatID = INVALID_INDEX;
		if (!materialView.clearcoatTex.empty())
		{
			auto pMaterialTex = GetOrLoadTexture(materialView.id, materialView.emissionTex);
			if (srvIndexCache.contains(pMaterialTex.get()))
			{
				material.clearcoatID = srvIndexCache[pMaterialTex.get()];
			}
			else
			{
				imageInfos.push_back({ m_pDefaultSampler->vkSampler(), pMaterialTex->vkView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
				material.clearcoatID = (u32)imageInfos.size() - 1;
				srvIndexCache.emplace(pMaterialTex.get(), material.clearcoatID);
			}
		}

		material.sheenID = INVALID_INDEX;
		if (!materialView.sheenTex.empty())
		{
			auto pMaterialTex = GetOrLoadTexture(materialView.id, materialView.sheenTex);
			if (srvIndexCache.contains(pMaterialTex.get()))
			{
				material.sheenID = srvIndexCache[pMaterialTex.get()];
			}
			else
			{
				imageInfos.push_back({ m_pDefaultSampler->vkSampler(), pMaterialTex->vkView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
				material.sheenID = (u32)imageInfos.size() - 1;
				srvIndexCache.emplace(pMaterialTex.get(), material.sheenID);
			}
		}

		material.anisotropyID = INVALID_INDEX;
		if (!materialView.anisotropyTex.empty())
		{
			auto pMaterialTex = GetOrLoadTexture(materialView.id, materialView.anisotropyTex);
			if (srvIndexCache.contains(pMaterialTex.get()))
			{
				material.anisotropyID = srvIndexCache[pMaterialTex.get()];
			}
			else
			{
				imageInfos.push_back({ m_pDefaultSampler->vkSampler(), pMaterialTex->vkView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
				material.anisotropyID = (u32)imageInfos.size() - 1;
				srvIndexCache.emplace(pMaterialTex.get(), material.anisotropyID);
			}
		}

		material.subsurfaceID = INVALID_INDEX;
		if (!materialView.subsurfaceTex.empty())
		{
			auto pMaterialTex = GetOrLoadTexture(materialView.id, materialView.subsurfaceTex);
			if (srvIndexCache.contains(pMaterialTex.get()))
			{
				material.subsurfaceID = srvIndexCache[pMaterialTex.get()];
			}
			else
			{
				imageInfos.push_back({ m_pDefaultSampler->vkSampler(), pMaterialTex->vkView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
				material.subsurfaceID = (u32)imageInfos.size() - 1;
				srvIndexCache.emplace(pMaterialTex.get(), material.subsurfaceID);
			}
		}

		material.transmissionID = INVALID_INDEX;
		if (!materialView.transmissionTex.empty())
		{
			auto pMaterialTex = GetOrLoadTexture(materialView.id, materialView.transmissionTex);
			if (srvIndexCache.contains(pMaterialTex.get()))
			{
				material.transmissionID = srvIndexCache[pMaterialTex.get()];
			}
			else
			{
				imageInfos.push_back({ m_pDefaultSampler->vkSampler(), pMaterialTex->vkView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
				material.transmissionID = (u32)imageInfos.size() - 1;
				srvIndexCache.emplace(pMaterialTex.get(), material.transmissionID);
			}
		}

		materials.push_back(material);
	}
	UpdateFrameBuffer(ctx, materials.data(), (u32)materials.size(), sizeof(MaterialData), *m_FrameData[m_ContextIndex].pMaterialAllocator, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);

	std::vector< MeshData > meshes;
	for (const auto& meshView : sceneView.meshes)
	{
		auto vHandle = GetOrUpdateVertex(meshView.id, meshView.tag, meshView.vData, meshView.vCount);

		MeshData mesh = {};
		mesh.vOffset = vHandle.offset;
		mesh.maxLOD  = meshView.maxLOD;

		for (u8 i = 0; i <= meshView.maxLOD; ++i)
		{
			std::string tag = meshView.tag + "_LOD" + std::to_string(i);

			auto iHandle  = GetOrUpdateIndex(meshView.id, tag, meshView.lods[i].iData, meshView.lods[i].iCount);
			auto mHandle  = GetOrUpdateMeshlets(meshView.id, tag, meshView.lods[i].mData, meshView.lods[i].mCount);
			auto mvHandle = GetOrUpdateMeshletVertices(meshView.id, tag, meshView.lods[i].mvData, meshView.lods[i].mvCount);
			auto mtHandle = GetOrUpdateMeshletTriangles(meshView.id, tag, meshView.lods[i].mtData, meshView.lods[i].mtCount);

			mesh.lods[i].mCount   = mHandle.count;
			mesh.lods[i].mOffset  = mHandle.offset;
			mesh.lods[i].mvOffset = mvHandle.offset;
			mesh.lods[i].mtOffset = mtHandle.offset;

			mesh.lods[i].simplifyError = meshView.lods[i].simplifyError;
		}

		mesh.center = meshView.sphere.Center();
		mesh.radius = meshView.sphere.Radius();

		meshes.push_back(mesh);
	}
	UpdateFrameBuffer(ctx, meshes.data(), (u32)meshes.size(), sizeof(MeshData), *m_FrameData[m_ContextIndex].pMeshDataAllocator, VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT | VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);

	u32 meshletVisibilityCursor = 0;
	std::vector< InstanceData > instances;
	for (auto& [id, data] : sceneView.draws)
	{
		InstanceData instance = {};
		if (data.mesh != INVALID_INDEX)
		{
			BB_ASSERT(data.mesh < sceneView.meshes.size(), "Mesh idx_%d should less than mesh size %d", data.mesh, (u32)sceneView.meshes.size());
			auto& meshView = sceneView.meshes[data.mesh];

			{
				instance.meshID = data.mesh;

				assert(data.transform != INVALID_INDEX && data.transform < sceneView.transforms.size());
				instance.transformID = data.transform;

				instance.materialID = INVALID_INDEX;
				if (data.material != INVALID_INDEX)
				{
					assert(data.material < sceneView.materials.size());
					instance.materialID = data.material;
				}

				instance.visOffset = meshletVisibilityCursor;

				u32 maxLodMeshletCount = 0;
				for (u8 i = 0; i <= meshView.maxLOD; ++i)
					maxLodMeshletCount = std::max(maxLodMeshletCount, meshes[data.mesh].lods[i].mCount);
				meshletVisibilityCursor += maxLodMeshletCount;
			}
			instances.push_back(instance);

			m_NumInstances++;
		}
	}
	m_NumMeshletVisibilitySlots = meshletVisibilityCursor;
	UpdateFrameBuffer(ctx, instances.data(), (u32)instances.size(), sizeof(InstanceData), *m_FrameData[m_ContextIndex].pInstanceAllocator, VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT | VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
	//UpdateFrameBuffer(ctx, indirects.data(), (u32)indirects.size(), sizeof(IndirectCommandData), *m_FrameData[m_ContextIndex].pIndirectCommandAllocator, VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT | VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);

	UpdateFrameBuffer(ctx, &sceneView.light, 1, sizeof(LightData), *m_FrameData[m_ContextIndex].pLightAllocator, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
	ctx.FlushBarriers();

	UpdateCameraAndEnvironment(sceneView, ctx);
}

void VkSceneResource::BindSceneResources(render::CommandContext& context)
{
	VkCommandContext& rhiContext = static_cast<VkCommandContext&>(context);
	
	auto vkDescriptorSet = m_pDescriptorPool->AllocateSet(m_vkSetLayout).vkDescriptorSet();
	if (rhiContext.IsGraphicsContext())
	{
		vkCmdBindDescriptorSets(
			rhiContext.vkCommandBuffer(),
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			rhiContext.vkGraphicsPipelineLayout(),
			0, 1, &vkDescriptorSet, 0, nullptr);
	}
	else if (rhiContext.IsComputeContext())
	{
		vkCmdBindDescriptorSets(
			rhiContext.vkCommandBuffer(),
			VK_PIPELINE_BIND_POINT_COMPUTE,
			rhiContext.vkComputePipelineLayout(),
			0, 1, &vkDescriptorSet, 0, nullptr);
	}
	else
	{
		vkCmdBindDescriptorSets(
			rhiContext.vkCommandBuffer(),
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			m_vkGlobalPipelineLayout,
			0, 1, &vkDescriptorSet, 0, nullptr);

		vkCmdBindDescriptorSets(
			rhiContext.vkCommandBuffer(),
			VK_PIPELINE_BIND_POINT_COMPUTE,
			m_vkGlobalPipelineLayout,
			0, 1, &vkDescriptorSet, 0, nullptr);
	}
}

BufferHandle VkSceneResource::GetOrUpdateVertex(u64 entity, const std::string& filepath, const void* pData, u32 count)
{
	auto& rm = static_cast<VkResourceManager&>(m_RenderDevice.GetResourceManager());

	auto it = m_VertexCache.find(filepath);
	if (it != m_VertexCache.end())
		return it->second;

	u64 sizeInBytes = sizeof(Vertex) * count;

	auto allocation = m_pVertexAllocator->Allocate(count, sizeof(Vertex));
	rm.UploadData(allocation.pBuffer, pData, sizeInBytes, VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT, allocation.offset * sizeof(Vertex));

	BufferHandle handle = {};
	handle.vkBuffer           = allocation.pBuffer->vkBuffer();
	handle.offset             = allocation.offset;
	handle.count              = count;
	handle.elementSizeInBytes = sizeof(Vertex);

	m_VertexCache.emplace(filepath, handle);
	return handle;
}

BufferHandle VkSceneResource::GetOrUpdateIndex(u64 entity, const std::string& filepath, const void* pData, u32 count)
{
	auto& rm = static_cast<VkResourceManager&>(m_RenderDevice.GetResourceManager());

	auto it = m_IndexCache.find(filepath);
	if (it != m_IndexCache.end())
		return it->second;

	u64 sizeInBytes = sizeof(Index) * count;

	auto allocation = m_pIndexAllocator->Allocate(count, sizeof(Index));
	rm.UploadData(allocation.pBuffer, pData, sizeInBytes, VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, allocation.offset * sizeof(Index));

	BufferHandle handle = {};
	handle.vkBuffer           = allocation.pBuffer->vkBuffer();
	handle.offset             = allocation.offset;
	handle.count              = count;
	handle.elementSizeInBytes = sizeof(Index);

	m_IndexCache.emplace(filepath, handle);
	return handle;
}

BufferHandle VkSceneResource::GetOrUpdateMeshlets(u64 entity, const std::string& filepath, const void* pData, u32 count)
{
	auto& rm = static_cast<VkResourceManager&>(m_RenderDevice.GetResourceManager());

	auto it = m_MeshletCache.find(filepath);
	if (it != m_MeshletCache.end())
		return it->second;

	u64 sizeInBytes = sizeof(Meshlet) * count;

	auto allocation = m_pMeshletAllocator->Allocate(count, sizeof(Meshlet));
	rm.UploadData(allocation.pBuffer, pData, sizeInBytes, VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT | VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT, allocation.offset * sizeof(Meshlet));

	BufferHandle handle = {};
	handle.vkBuffer           = allocation.pBuffer->vkBuffer();
	handle.offset             = allocation.offset;
	handle.count              = count;
	handle.elementSizeInBytes = sizeof(Meshlet);

	m_MeshletCache.emplace(filepath, handle);
	return handle;
}

BufferHandle VkSceneResource::GetOrUpdateMeshletVertices(u64 entity, const std::string& filepath, const void* pData, u32 count)
{
	auto& rm = static_cast<VkResourceManager&>(m_RenderDevice.GetResourceManager());

	auto it = m_MeshletVertexCache.find(filepath);
	if (it != m_MeshletVertexCache.end())
		return it->second;

	u64 sizeInBytes = sizeof(u32) * count;

	auto allocation = m_pMeshletVertexAllocator->Allocate(count, sizeof(u32));
	rm.UploadData(allocation.pBuffer, pData, sizeInBytes, VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT, allocation.offset * sizeof(u32));

	BufferHandle handle = {};
	handle.vkBuffer           = allocation.pBuffer->vkBuffer();
	handle.offset             = allocation.offset;
	handle.count              = count;
	handle.elementSizeInBytes = sizeof(u32);

	m_MeshletVertexCache.emplace(filepath, handle);
	return handle;
}

BufferHandle VkSceneResource::GetOrUpdateMeshletTriangles(u64 entity, const std::string& filepath, const void* pData, u32 count)
{
	auto& rm = static_cast<VkResourceManager&>(m_RenderDevice.GetResourceManager());

	auto it = m_MeshletTriangleCache.find(filepath);
	if (it != m_MeshletTriangleCache.end())
		return it->second;

	auto allocation = m_pMeshletTriangleAllocator->Allocate(count, sizeof(u32));
	rm.UploadData(allocation.pBuffer, pData, sizeof(u32) * count, VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT, sizeof(u32) * allocation.offset);

	BufferHandle handle = {};
	handle.vkBuffer           = allocation.pBuffer->vkBuffer();
	handle.offset             = allocation.offset;
	handle.count              = count;
	handle.elementSizeInBytes = sizeof(u32);

	m_MeshletTriangleCache.emplace(filepath, handle);
	return handle;
}

Arc< VulkanTexture > VkSceneResource::GetOrLoadTexture(u64 entity, const std::string& filepath)
{
	auto& rm = m_RenderDevice.GetResourceManager();

	auto it = m_TextureCache.find(filepath);
	if (it != m_TextureCache.end())
		return it->second;

	auto tex   = rm.LoadTexture(filepath);
	auto vkTex = StaticCast<VulkanTexture>(tex);

	m_TextureCache.emplace(filepath, vkTex);

	return vkTex;
}

Arc< VulkanTexture > VkSceneResource::GetTexture(const std::string& filepath)
{
	std::string f = filepath.data();
	auto it = m_TextureCache.find(filepath);
	if (it != m_TextureCache.end())
		return it->second;

	return nullptr;
}

void VkSceneResource::ResetFrameBuffers()
{
	m_NumInstances              = 0;
	m_NumMeshletVisibilitySlots = 0;

	m_FrameData[m_ContextIndex].Reset();
}

void VkSceneResource::UpdateFrameBuffer(VkCommandContext& context, const void* pData, u32 count, u64 elementSizeInBytes, StaticBufferAllocator& targetBuffer, VkPipelineStageFlags2 dstStageMask)
{
	if (count == 0 || elementSizeInBytes == 0)
		return;

	auto allocation = targetBuffer.Allocate(count, elementSizeInBytes);
	context.UploadData(allocation.pBuffer, pData, count, elementSizeInBytes, allocation.offset);
	context.TransitionBufferToRead(allocation.pBuffer, dstStageMask, allocation.offset, true);
}

VkDescriptorSet VkSceneResource::GetSceneDescriptorSet() const
{
	return m_pDescriptorPool->AllocateSet(m_vkSetLayout).vkDescriptorSet();
}

VkDescriptorBufferInfo VkSceneResource::GetIndexBufferInfo() const
{
	return m_pIndexAllocator->GetDescriptorInfo();
}

VkDescriptorBufferInfo VkSceneResource::GetMeshletBufferInfo() const
{
	return m_pMeshletAllocator->GetDescriptorInfo();
}

VkDescriptorBufferInfo VkSceneResource::GetInstanceInfo() const
{
	return m_FrameData[m_ContextIndex].pInstanceAllocator->GetDescriptorInfo();
}

const Arc< render::Buffer >& VkSceneResource::GetArgumentBuffer() const
{
	return nullptr; // m_FrameData[m_ContextIndex].pIndirectCommandAllocator->GetAllocationBuffer();
}
} // namespace vk