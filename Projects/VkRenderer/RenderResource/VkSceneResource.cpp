#include "RendererPch.h"
#include "VkSceneResource.h"
#include "RenderDevice/VkDescriptorSet.h"
#include "RenderDevice/VkDescriptorPool.h"
#include "RenderDevice/VkResourceManager.h"
#include "RenderDevice/VkBufferAllocator.h"
#include "RenderDevice/VkRenderPipeline.h"
#include "RenderDevice/VkCommandContext.h"
#include "RenderResource/VkShader.h"
#include "SceneRenderView.h"
#include "Utils/Math.hpp"

namespace vk
{

enum
{
	eStaticSetBindingIndex_CombinedImage2D = 0,
	eStaticSetBindingIndex_Vertex          = 1,
	eStaticSetBindingIndex_IndirectDraw    = 2,
	eStaticSetBindingIndex_Transform       = 3,

	eStaticSetBindingIndex_Material = 4,
	eStaticSetBindingIndex_Lighting = 5,
};

static VulkanComputePipeline* s_CombineTexturesPipeline = nullptr;
Arc< VulkanTexture > CombineTextures(VkRenderDevice& rd, const std::string& name, Arc< VulkanTexture > pTextureR, Arc< VulkanTexture > pTextureG, Arc< VulkanTexture > pTextureB, Arc< VulkanSampler > pSampler)
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
		subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		subresourceRange.baseMipLevel = 0;
		subresourceRange.levelCount = pCombinedTexture->Desc().mipLevels;
		subresourceRange.layerCount = pCombinedTexture->Desc().arrayLayers;
		pContext->TransitionImageLayout(pTextureR, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, subresourceRange);
		pContext->TransitionImageLayout(pTextureG, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, subresourceRange);
		pContext->TransitionImageLayout(pTextureB, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, subresourceRange);
		pContext->TransitionImageLayout(pCombinedTexture, VK_IMAGE_LAYOUT_GENERAL, subresourceRange);
		pContext->PushDescriptor(
			0, 
			{ pSampler->vkSampler(), pTextureR->vkView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
		pContext->PushDescriptor(
			1,
			{ pSampler->vkSampler(), pTextureG->vkView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
		pContext->PushDescriptor(
			2,
			{ pSampler->vkSampler(), pTextureB->vkView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
		pContext->PushDescriptor(
			3,
			{ pSampler->vkSampler(), pCombinedTexture->vkView(), VK_IMAGE_LAYOUT_GENERAL },
			VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);

		pContext->Dispatch2D< 16, 16 >(width, height);

		pContext->TransitionImageLayout(pCombinedTexture, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, subresourceRange);
		pContext->Close();

		rd.ExecuteCommand(pContext);
	}

	return pCombinedTexture;
}

VkSceneResource::VkSceneResource(VkRenderDevice& rd)
	: m_RenderDevice(rd)
{
	// **
	// scene buffers
	// **
	m_pVertexAllocator       = MakeBox< StaticBufferAllocator >(m_RenderDevice, sizeof(Vertex) * _MB(8));
	m_pIndexAllocator        = MakeBox< StaticBufferAllocator >(m_RenderDevice, sizeof(Index) * 3 * _MB(8), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
	m_pIndirectDrawAllocator = MakeBox< StaticBufferAllocator >(m_RenderDevice, sizeof(IndirectDrawData) * _KB(8), VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
	m_pTransformAllocator    = MakeBox< StaticBufferAllocator >(m_RenderDevice, sizeof(TransformData) * _KB(8));
	m_pMaterialAllocator     = MakeBox< StaticBufferAllocator >(m_RenderDevice, sizeof(MaterialData) * _KB(8));
	m_pLightAllocator        = MakeBox< StaticBufferAllocator >(m_RenderDevice, sizeof(LightData));

	// **
	// scene descriptor pool
	// **
	std::vector< VkDescriptorPoolSize > poolSizes =
	{
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 8 },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAX_BINDLESS_DESCRIPTOR_RESOURCE_COUNT },
	};
	m_pDescriptorPool = new DescriptorPool(m_RenderDevice, std::move(poolSizes), 1, VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT);

	std::vector< VkDescriptorSetLayoutBinding > bindings =
	{
		{ eStaticSetBindingIndex_CombinedImage2D, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAX_BINDLESS_DESCRIPTOR_RESOURCE_COUNT, VK_SHADER_STAGE_FRAGMENT_BIT },
		{ eStaticSetBindingIndex_Vertex, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT },
		{ eStaticSetBindingIndex_IndirectDraw, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT },
		{ eStaticSetBindingIndex_Transform, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT },
		{ eStaticSetBindingIndex_Material, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT },
		{ eStaticSetBindingIndex_Lighting, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT },
	};

	std::vector < VkDescriptorBindingFlags > flags =
	{
		VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT_EXT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT_EXT,
		VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT_EXT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT_EXT,
		VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT_EXT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT_EXT,
		VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT_EXT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT_EXT,
		VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT_EXT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT_EXT,
		VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT_EXT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT_EXT,
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
	// default sampler for scene textures
	// **
	m_pDefaultSampler = VulkanSampler::Create(m_RenderDevice, "DefaultSampler", {});
}

VkSceneResource::~VkSceneResource()
{
	imageInfos.clear();

	vkDestroyDescriptorSetLayout(m_RenderDevice.vkDevice(), m_vkSetLayout, nullptr);
	RELEASE(m_pDescriptorPool);

	RELEASE(s_CombineTexturesPipeline);
}

void VkSceneResource::UpdateSceneResources(const SceneRenderView& sceneView)
{
	auto& rm = m_RenderDevice.GetResourceManager();

	ResetFrameBuffers();

	std::vector< TransformData > transforms;
	transforms.reserve(sceneView.transforms.size());
	for (auto& transformView : sceneView.transforms)
	{
		TransformData transform = {};
		transform.mWorldToView  = transformView.mWorld;
		transform.mViewToWorld  = glm::inverse(transformView.mWorld);
		transforms.push_back(transform);
	}
	UpdateFrameBuffer(transforms.data(), (u32)transforms.size(), sizeof(TransformData), *m_pTransformAllocator);

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
			auto pAlbedo = GetOrLoadTexture(materialView.id, materialView.albedoTex);
			if (srvIndexCache.contains(pAlbedo.get()))
			{
				material.albedoID = srvIndexCache[pAlbedo.get()];
			}
			else
			{
				imageInfos.push_back({ m_pDefaultSampler->vkSampler(), pAlbedo->vkView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
				material.albedoID = (u32)imageInfos.size() - 1;
				srvIndexCache.emplace(pAlbedo.get(), material.albedoID);
			}
		}

		material.normalID = INVALID_INDEX;
		if (!materialView.normalTex.empty())
		{
			auto pNormal = GetOrLoadTexture(materialView.id, materialView.normalTex);
			if (srvIndexCache.contains(pNormal.get()))
			{
				material.normalID = srvIndexCache[pNormal.get()];
			}
			else
			{
				imageInfos.push_back({ m_pDefaultSampler->vkSampler(), pNormal->vkView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
				material.normalID = (u32)imageInfos.size() - 1;
				srvIndexCache.emplace(pNormal.get(), material.normalID);
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

		material.emissionID = INVALID_INDEX;
		if (!materialView.emissionTex.empty())
		{
			auto pEmission = GetOrLoadTexture(materialView.id, materialView.emissionTex);
			if (srvIndexCache.contains(pEmission.get()))
			{
				material.emissionID = srvIndexCache[pEmission.get()];
			}
			else
			{
				imageInfos.push_back({ m_pDefaultSampler->vkSampler(), pEmission->vkView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
				material.emissionID = (u32)imageInfos.size() - 1;
				srvIndexCache.emplace(pEmission.get(), material.emissionID);
			}
		}

		materials.push_back(material);
	}
	UpdateFrameBuffer(materials.data(), (u32)materials.size(), sizeof(MaterialData), *m_pMaterialAllocator);

	std::vector< IndirectDrawData > indirects;
	for (auto& [id, data] : sceneView.draws)
	{
		IndirectDrawData indirect = {};
		if (data.mesh != INVALID_INDEX)
		{
			BB_ASSERT(data.mesh < sceneView.meshes.size(), "Mesh idx_%d should less than mesh size %d", data.mesh, (u32)sceneView.meshes.size());
			auto& meshView = sceneView.meshes[data.mesh];

			auto vertex = GetOrUpdateVertex(meshView.id, meshView.tag, meshView.vData, meshView.vCount);
			auto index  = GetOrUpdateIndex(meshView.id, meshView.tag, meshView.iData, meshView.iCount);

			indirect.indexCount    = index.count;
			indirect.instanceCount = 1;
			indirect.firstIndex    = index.offset;
			indirect.vertexOffset  = vertex.offset;
			indirect.firstInstance = 0;

			indirect.materialIndex = INVALID_INDEX;
			if (data.material != INVALID_INDEX)
			{
				assert(data.material < sceneView.materials.size());
				indirect.materialIndex = data.material;
			}

			assert(data.transform != INVALID_INDEX && data.transform < sceneView.transforms.size());
			indirect.transformID    = data.transform;
			indirect.transformCount = (u32)transforms.size();

			indirects.push_back(indirect);
		}
	}
	UpdateFrameBuffer(indirects.data(), (u32)indirects.size(), sizeof(IndirectDrawData), *m_pIndirectDrawAllocator);
	
	UpdateFrameBuffer(&sceneView.light, 1, sizeof(LightData), *m_pLightAllocator);

	auto& descriptorSet = m_pDescriptorPool->AllocateSet(m_vkSetLayout);
	descriptorSet.StageDescriptors(imageInfos, eStaticSetBindingIndex_CombinedImage2D, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
	descriptorSet.StageDescriptor(m_pVertexAllocator->GetDescriptorInfo(), eStaticSetBindingIndex_Vertex, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
	descriptorSet.StageDescriptor(m_pIndirectDrawAllocator->GetDescriptorInfo(), eStaticSetBindingIndex_IndirectDraw, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
	descriptorSet.StageDescriptor(m_pTransformAllocator->GetDescriptorInfo(), eStaticSetBindingIndex_Transform, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
	descriptorSet.StageDescriptor(m_pMaterialAllocator->GetDescriptorInfo(), eStaticSetBindingIndex_Material, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
	descriptorSet.StageDescriptor(m_pLightAllocator->GetDescriptorInfo(), eStaticSetBindingIndex_Lighting, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
}

void VkSceneResource::BindSceneResources(render::CommandContext& context)
{
	VkCommandContext& rhicontext = static_cast<VkCommandContext&>(context);

	auto vkDescriptorSet = m_pDescriptorPool->AllocateSet(m_vkSetLayout).vkDescriptorSet();
	if (rhicontext.IsGraphicsContext())
	{
		vkCmdBindDescriptorSets(
			rhicontext.vkCommandBuffer(),
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			rhicontext.vkGraphicsPipelineLayout(),
			eDescriptorSet_Static, 1, &vkDescriptorSet, 0, nullptr);
	}
	else
	{
		vkCmdBindDescriptorSets(
			rhicontext.vkCommandBuffer(),
			VK_PIPELINE_BIND_POINT_COMPUTE,
			rhicontext.vkComputePipelineLayout(),
			eDescriptorSet_Static, 1, &vkDescriptorSet, 0, nullptr);
	}
}

BufferHandle VkSceneResource::GetOrUpdateVertex(u64 entity, const std::string& filepath, const void* pData, u32 count)
{
	auto& rm = static_cast<VkResourceManager&>(m_RenderDevice.GetResourceManager());

	if (m_VertexCache.contains(filepath))
	{
		return m_VertexCache.find(filepath)->second;
	}

	u64 sizeInBytes = sizeof(Vertex) * count;

	auto allocation = m_pVertexAllocator->Allocate(count, sizeof(Vertex));
	rm.UploadData(allocation.vkBuffer, pData, sizeInBytes, VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, allocation.offset * sizeof(Vertex));

	BufferHandle handle = {};
	handle.vkBuffer           = allocation.vkBuffer;
	handle.offset             = allocation.offset;
	handle.count              = count;
	handle.elementSizeInBytes = sizeof(Vertex);

	m_VertexCache.emplace(filepath, handle);
	return handle;
}

BufferHandle VkSceneResource::GetOrUpdateIndex(u64 entity, const std::string& filepath, const void* pData, u32 count)
{
	auto& rm = static_cast<VkResourceManager&>(m_RenderDevice.GetResourceManager());

	if (m_IndexCache.contains(filepath))
	{
		return m_IndexCache.find(filepath)->second;
	}

	u64 sizeInBytes = sizeof(Index) * count;

	auto allocation = m_pIndexAllocator->Allocate(count, sizeof(Index));
	rm.UploadData(allocation.vkBuffer, pData, sizeInBytes, VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, allocation.offset * sizeof(Index));

	BufferHandle handle       = {};
	handle.vkBuffer           = allocation.vkBuffer;
	handle.offset             = allocation.offset;
	handle.count              = count;
	handle.elementSizeInBytes = sizeof(Index);

	m_IndexCache.emplace(filepath, handle);
	return handle;
}

Arc< VulkanTexture > VkSceneResource::GetOrLoadTexture(u64 entity, const std::string& filepath)
{
	auto& rm = m_RenderDevice.GetResourceManager();

	if (m_TextureCache.contains(filepath))
	{
		return m_TextureCache.find(filepath)->second;
	}

	auto tex   = rm.LoadTexture(filepath);
	auto vkTex = StaticCast<VulkanTexture>(tex);

	m_TextureCache.emplace(filepath, vkTex);

	return vkTex;
}

Arc< VulkanTexture > VkSceneResource::GetTexture(const std::string& filepath)
{
	std::string f = filepath.data();
	if (m_TextureCache.contains(f))
	{
		return m_TextureCache.find(f)->second;
	}

	return nullptr;
}

void VkSceneResource::ResetFrameBuffers()
{
	m_pIndirectDrawAllocator->Reset();
	m_pTransformAllocator->Reset();
	m_pMaterialAllocator->Reset();
	m_pLightAllocator->Reset();
}

void VkSceneResource::UpdateFrameBuffer(const void* pData, u32 count, u64 elementSizeInBytes, StaticBufferAllocator& targetBuffer)
{
	if (count == 0 || elementSizeInBytes == 0)
		return;

	auto& rm = static_cast<VkResourceManager&>(m_RenderDevice.GetResourceManager());

	u64 sizeInBytes = count * elementSizeInBytes;

	auto allocation = targetBuffer.Allocate(count, elementSizeInBytes);
	rm.UploadData(allocation.vkBuffer, pData, sizeInBytes, VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, allocation.offset);
}

VkDescriptorSet VkSceneResource::GetSceneDescriptorSet() const
{
	return m_pDescriptorPool->AllocateSet(m_vkSetLayout).vkDescriptorSet();
}

VkDescriptorBufferInfo VkSceneResource::GetIndexBufferInfo() const
{
	return m_pIndexAllocator->GetDescriptorInfo();
}

VkDescriptorBufferInfo VkSceneResource::GetIndirectBufferInfo() const
{
	return m_pIndirectDrawAllocator->GetDescriptorInfo();
}

} // namespace vk