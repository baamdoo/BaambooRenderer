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

#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

namespace vk
{

static ComputePipeline* s_CombineTexturesPipeline = nullptr;
Arc< Texture > CombineTextures(RenderDevice& renderDevice, const std::string& name, Arc< Texture > pTextureR, Arc< Texture > pTextureG, Arc< Texture > pTextureB, Arc< Sampler > pSampler)
{
	u32 width 
		= std::max({ pTextureR->Desc().extent.width, pTextureG->Desc().extent.width, pTextureB->Desc().extent.width });
	u32 height 
		= std::max({ pTextureR->Desc().extent.height, pTextureG->Desc().extent.height, pTextureB->Desc().extent.height });
	auto pCombinedTexture =
		Texture::Create(
			renderDevice,
			name,
			{
				.resolution = { width, height, 1 },
				.imageUsage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT
			});

	auto& context = renderDevice.BeginCommand(eCommandType::Compute, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, true);
	{
		context.SetRenderPipeline(s_CombineTexturesPipeline);

		VkImageSubresourceRange subresourceRange = {};
		subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		subresourceRange.baseMipLevel = 0;
		subresourceRange.levelCount = pCombinedTexture->Desc().mipLevels;
		subresourceRange.layerCount = pCombinedTexture->Desc().arrayLayers;
		context.TransitionImageLayout(pTextureR, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, subresourceRange);
		context.TransitionImageLayout(pTextureG, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, subresourceRange);
		context.TransitionImageLayout(pTextureB, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, subresourceRange);
		context.TransitionImageLayout(pCombinedTexture, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, subresourceRange);
		context.PushDescriptors(
			0, 
			{ pSampler->vkSampler(), pTextureR->vkView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }, 
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
		context.PushDescriptors(
			1,
			{ pSampler->vkSampler(), pTextureG->vkView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
		context.PushDescriptors(
			2,
			{ pSampler->vkSampler(), pTextureB->vkView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
		context.PushDescriptors(
			3,
			{ pSampler->vkSampler(), pCombinedTexture->vkView(), VK_IMAGE_LAYOUT_GENERAL },
			VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);

		context.Dispatch2D< 16, 16 >(width, height);

		context.TransitionImageLayout(pCombinedTexture, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, subresourceRange);
		context.Close();
	}
	context.Execute();

	return pCombinedTexture;
}

SceneResource::SceneResource(RenderDevice& device)
	: m_RenderDevice(device)
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
		{ eStaticSetBindingIndex_Material, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT },
		{ eStaticSetBindingIndex_Lighting, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT },
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
	m_pDefaultSampler = Sampler::Create(m_RenderDevice, "DefaultSampler", {});
}

SceneResource::~SceneResource()
{
	imageInfos.clear();

	vkDestroyDescriptorSetLayout(m_RenderDevice.vkDevice(), m_vkSetLayout, nullptr);
	RELEASE(m_pDescriptorPool);

	RELEASE(s_CombineTexturesPipeline);
}

void SceneResource::UpdateSceneResources(const SceneRenderView& sceneView)
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
	imageInfos.push_back({ m_pDefaultSampler->vkSampler(), rm.GetFlatWhiteTexture()->vkView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
	imageInfos.push_back({ m_pDefaultSampler->vkSampler(), rm.GetFlatBlackTexture()->vkView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
	imageInfos.push_back({ m_pDefaultSampler->vkSampler(), rm.GetFlatGrayTexture()->vkView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });

	std::vector< MaterialData > materials;
	materials.reserve(sceneView.materials.size());
	std::unordered_map< Texture*, u32 > srvIndexCache;
	for (auto& materialView : sceneView.materials)
	{
		MaterialData material = {};
		material.tint         = materialView.tint;
		material.roughness    = materialView.roughness;
		material.metallic     = materialView.metallic;

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
				s_CombineTexturesPipeline = new ComputePipeline(m_RenderDevice, "CombineTextures");
				Arc< Shader > pCS
					= Shader::Create(m_RenderDevice, "CombineTextures", { .filepath = SPIRV_PATH.string() + "CombineTextures.comp.spv" });
				s_CombineTexturesPipeline->SetComputeShader(std::move(pCS)).Build();
			}

			Arc< Texture > pCombiningTextures[3] = {};
			if (!materialView.aoTex.empty())
				pCombiningTextures[0] = GetOrLoadTexture(materialView.id, materialView.aoTex);
			else
				pCombiningTextures[0] = rm.GetFlatWhiteTexture();

			if (!materialView.roughnessTex.empty())
				pCombiningTextures[1] = GetOrLoadTexture(materialView.id, materialView.roughnessTex);
			else
				pCombiningTextures[1] = rm.GetFlatGrayTexture();

			if (!materialView.metallicTex.empty())
				pCombiningTextures[2] = GetOrLoadTexture(materialView.id, materialView.metallicTex);
			else
				pCombiningTextures[2] = rm.GetFlatBlackTexture();

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
			assert(data.mesh < sceneView.meshes.size());
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
	
	UpdateFrameBuffer(&sceneView.light.data, 1, sizeof(LightData), *m_pLightAllocator);

	auto& descriptorSet = m_pDescriptorPool->AllocateSet(m_vkSetLayout);
	descriptorSet.StageDescriptors(imageInfos, eStaticSetBindingIndex_CombinedImage2D, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
	descriptorSet.StageDescriptor(m_pVertexAllocator->GetDescriptorInfo(), eStaticSetBindingIndex_Vertex, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
	descriptorSet.StageDescriptor(m_pIndirectDrawAllocator->GetDescriptorInfo(), eStaticSetBindingIndex_IndirectDraw, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
	descriptorSet.StageDescriptor(m_pTransformAllocator->GetDescriptorInfo(), eStaticSetBindingIndex_Transform, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
	descriptorSet.StageDescriptor(m_pMaterialAllocator->GetDescriptorInfo(), eStaticSetBindingIndex_Material, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
	descriptorSet.StageDescriptor(m_pLightAllocator->GetDescriptorInfo(), eStaticSetBindingIndex_Lighting, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
}

BufferHandle SceneResource::GetOrUpdateVertex(u32 entity, const std::string& filepath, const void* pData, u32 count)
{
	auto& rm = m_RenderDevice.GetResourceManager();

	if (m_VertexCache.contains(filepath))
	{
		return m_VertexCache.find(filepath)->second;
	}

	u64 sizeInBytes = sizeof(Vertex) * count;

	auto allocation = m_pVertexAllocator->Allocate(count, sizeof(Vertex));
	rm.UploadData(allocation.vkBuffer, pData, sizeInBytes, VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, allocation.offset * sizeof(Vertex));

	BufferHandle handle       = {};
	handle.vkBuffer           = allocation.vkBuffer;
	handle.offset             = allocation.offset;
	handle.count              = count;
	handle.elementSizeInBytes = sizeof(Vertex);

	m_VertexCache.emplace(filepath, handle);
	return handle;
}

BufferHandle SceneResource::GetOrUpdateIndex(u32 entity, const std::string& filepath, const void* pData, u32 count)
{
	auto& rm = m_RenderDevice.GetResourceManager();

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

Arc< Texture > SceneResource::GetOrLoadTexture(u32 entity, const std::string& filepath)
{
	auto& rm = m_RenderDevice.GetResourceManager();

	if (m_TextureCache.contains(filepath))
	{
		return m_TextureCache.find(filepath)->second;
	}

	fs::path path = filepath;

	u32 width, height, numChannels;
	u8* pData = stbi_load(path.string().c_str(), (int*)&width, (int*)&height, (int*)&numChannels, STBI_rgb_alpha);
	BB_ASSERT(pData, "No texture found on the path: %s", path.string().c_str());

	auto pTex  = Texture::Create(m_RenderDevice, path.filename().string(), 
		{
			.resolution = { width, height, 1 },
			.format     = VK_FORMAT_R8G8B8A8_UNORM,
			.imageUsage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
		});

	// **
	// Copy data to staging buffer
	// **
	auto texSizeInBytes = pTex->SizeInBytes();
	VkBufferImageCopy region = {};
	region.bufferOffset      = 0;
	region.bufferRowLength   = 0;
	region.bufferImageHeight = 0;
	region.imageSubresource  = {
		.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
		.mipLevel       = 0,
		.baseArrayLayer = 0,
		.layerCount     = 1
	};
	region.imageExtent       = { width, height, 1 };

	rm.UploadData(pTex, (void*)pData, texSizeInBytes, region);

	m_TextureCache.emplace(filepath, pTex);

	RELEASE(pData);
	return pTex;
}

Arc< Texture > SceneResource::GetTexture(const std::string& filepath)
{
	std::string f = filepath.data();
	if (m_TextureCache.contains(f))
	{
		return m_TextureCache.find(f)->second;
	}

	return nullptr;
}

void SceneResource::ResetFrameBuffers()
{
	m_pIndirectDrawAllocator->Reset();
	m_pTransformAllocator->Reset();
	m_pMaterialAllocator->Reset();
	m_pLightAllocator->Reset();
}

void SceneResource::UpdateFrameBuffer(const void* pData, u32 count, u64 elementSizeInBytes, StaticBufferAllocator& targetBuffer)
{
	if (count == 0 || elementSizeInBytes == 0)
		return;

	auto& rm = m_RenderDevice.GetResourceManager();

	u64 sizeInBytes = count * elementSizeInBytes;

	auto allocation = targetBuffer.Allocate(count, elementSizeInBytes);
	rm.UploadData(allocation.vkBuffer, pData, sizeInBytes, VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, allocation.offset);
}

VkDescriptorSet SceneResource::GetSceneDescriptorSet() const
{
	return m_pDescriptorPool->AllocateSet(m_vkSetLayout).vkDescriptorSet();
}

VkDescriptorBufferInfo SceneResource::GetIndexBufferInfo() const
{
	return m_pIndexAllocator->GetDescriptorInfo();
}

VkDescriptorBufferInfo SceneResource::GetIndirectBufferInfo() const
{
	return m_pIndirectDrawAllocator->GetDescriptorInfo();
}

} // namespace vk