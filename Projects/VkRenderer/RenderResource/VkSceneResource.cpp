#include "RendererPch.h"
#include "VkSceneResource.h"
#include "RenderDevice/VkDescriptorSet.h"
#include "RenderDevice/VkDescriptorPool.h"
#include "RenderDevice/VkResourceManager.h"
#include "RenderDevice/VkBufferAllocator.h"
#include "SceneRenderView.h"
#include "Utils/Math.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

namespace vk
{

SceneResource::SceneResource(RenderDevice& device)
	: m_RenderDevice(device)
{
	// **
	// scene buffers
	// **
	m_pVertexBufferPool       = new StaticBufferAllocator(m_RenderDevice, sizeof(Vertex) * _KB(8));
	m_pIndexBufferPool        = new StaticBufferAllocator(m_RenderDevice, sizeof(Index) * 3 * _KB(8), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
	m_pIndirectDrawBufferPool = new StaticBufferAllocator(m_RenderDevice, sizeof(IndirectDrawData) * _KB(8), VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
	m_pTransformBufferPool    = new StaticBufferAllocator(m_RenderDevice, sizeof(TransformData) * _KB(8));
	m_pMaterialBufferPool     = new StaticBufferAllocator(m_RenderDevice, sizeof(MaterialData) * _KB(8));

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
	};

	std::vector < VkDescriptorBindingFlags > flags =
	{
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
	vkDestroyDescriptorSetLayout(m_RenderDevice.vkDevice(), m_vkSetLayout, nullptr);
	RELEASE(m_pDescriptorPool);

	RELEASE(m_pMaterialBufferPool);
	RELEASE(m_pTransformBufferPool);
	RELEASE(m_pIndirectDrawBufferPool);
	RELEASE(m_pIndexBufferPool);
	RELEASE(m_pVertexBufferPool);
}

void SceneResource::UpdateSceneResources(const SceneRenderView& sceneView)
{
	ResetFrameBuffers();

	std::vector< TransformData > transforms;
	transforms.reserve(sceneView.transforms.size());
	for (auto& transformView : sceneView.transforms)
	{
		TransformData transform = {};
		transform.mWorldToView = transformView.mWorld;
		transform.mViewToWorld = glm::inverse(transformView.mWorld);
		transforms.push_back(transform);
	}
	UpdateFrameBuffer(transforms.data(), (u32)transforms.size(), sizeof(TransformData), m_pTransformBufferPool);

	imageInfos.clear();
	std::vector< MaterialData > materials;
	materials.reserve(sceneView.materials.size());
	std::unordered_map< Texture*, u32 > srvIndexCache;
	for (auto& materialView : sceneView.materials)
	{
		MaterialData material = {};
		material.tint      = materialView.tint;
		material.roughness = materialView.roughness;
		material.metallic  = materialView.metallic;

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

		material.specularID = INVALID_INDEX;
		if (!materialView.specularTex.empty())
		{
			auto pSpecular = GetOrLoadTexture(materialView.id, materialView.specularTex);
			if (srvIndexCache.contains(pSpecular.get()))
			{
				material.specularID = srvIndexCache[pSpecular.get()];
			}
			else
			{
				imageInfos.push_back({ m_pDefaultSampler->vkSampler(), pSpecular->vkView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
				material.specularID = (u32)imageInfos.size() - 1;
				srvIndexCache.emplace(pSpecular.get(), material.specularID);
			}
		}

		material.aoID = INVALID_INDEX;
		if (!materialView.aoTex.empty())
		{
			auto pAo = GetOrLoadTexture(materialView.id, materialView.aoTex);
			if (srvIndexCache.contains(pAo.get()))
			{
				material.aoID = srvIndexCache[pAo.get()];
			}
			else
			{
				imageInfos.push_back({ m_pDefaultSampler->vkSampler(), pAo->vkView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
				material.aoID = (u32)imageInfos.size() - 1;
				srvIndexCache.emplace(pAo.get(), material.aoID);
			}
		}

		material.roughnessID = INVALID_INDEX;
		if (!materialView.roughnessTex.empty())
		{
			auto pRoughness = GetOrLoadTexture(materialView.id, materialView.roughnessTex);
			if (srvIndexCache.contains(pRoughness.get()))
			{
				material.roughnessID = srvIndexCache[pRoughness.get()];
			}
			else
			{
				imageInfos.push_back({ m_pDefaultSampler->vkSampler(), pRoughness->vkView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
				material.roughnessID = (u32)imageInfos.size() - 1;
				srvIndexCache.emplace(pRoughness.get(), material.roughnessID);
			}
		}

		material.metallicID = INVALID_INDEX;
		if (!materialView.metallicTex.empty())
		{
			auto pMetallic = GetOrLoadTexture(materialView.id, materialView.metallicTex);
			if (srvIndexCache.contains(pMetallic.get()))
			{
				material.metallicID = srvIndexCache[pMetallic.get()];
			}
			else
			{
				imageInfos.push_back({ m_pDefaultSampler->vkSampler(), pMetallic->vkView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
				material.metallicID = (u32)imageInfos.size() - 1;
				srvIndexCache.emplace(pMetallic.get(), material.metallicID);
			}
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
	UpdateFrameBuffer(materials.data(), (u32)materials.size(), sizeof(MaterialData), m_pMaterialBufferPool);

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
	UpdateFrameBuffer(indirects.data(), (u32)indirects.size(), sizeof(IndirectDrawData), m_pIndirectDrawBufferPool);

	auto& descriptorSet = m_pDescriptorPool->AllocateSet(m_vkSetLayout);
	descriptorSet.StageDescriptors(imageInfos, eStaticSetBindingIndex_CombinedImage2D, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
	descriptorSet.StageDescriptor(GetVertexDescriptorInfo(), eStaticSetBindingIndex_Vertex, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
	descriptorSet.StageDescriptor(GetIndirectDrawDescriptorInfo(), eStaticSetBindingIndex_IndirectDraw, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
	descriptorSet.StageDescriptor(GetTransformDescriptorInfo(), eStaticSetBindingIndex_Transform, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
	descriptorSet.StageDescriptor(GetMaterialDescriptorInfo(), eStaticSetBindingIndex_Material, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
}

BufferHandle SceneResource::GetOrUpdateVertex(u32 entity, std::string_view filepath, const void* pData, u32 count)
{
	auto& rm = m_RenderDevice.GetResourceManager();

	std::string f = filepath.data();
	if (m_VertexCache.contains(f))
	{
		return m_VertexCache.find(f)->second;
	}

	u64 sizeInBytes = sizeof(Vertex) * count;

	auto allocation = m_pVertexBufferPool->Allocate(count, sizeof(Vertex));
	rm.UploadData(allocation.vkBuffer, pData, sizeInBytes, VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, allocation.offset);

	BufferHandle handle = {};
	handle.vkBuffer           = allocation.vkBuffer;
	handle.offset             = allocation.offset;
	handle.count              = count;
	handle.elementSizeInBytes = sizeof(Vertex);

	m_VertexCache.emplace(filepath, handle);
	return handle;
}

BufferHandle SceneResource::GetOrUpdateIndex(u32 entity, std::string_view filepath, const void* pData, u32 count)
{
	auto& rm = m_RenderDevice.GetResourceManager();

	std::string f = filepath.data();
	if (m_IndexCache.contains(f))
	{
		return m_IndexCache.find(f)->second;
	}

	u64 sizeInBytes = sizeof(Index) * count;

	auto allocation = m_pIndexBufferPool->Allocate(count, sizeof(Index));
	rm.UploadData(allocation.vkBuffer, pData, sizeInBytes, VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, allocation.offset);

	BufferHandle handle = {};
	handle.vkBuffer           = allocation.vkBuffer;
	handle.offset             = allocation.offset;
	handle.count              = count;
	handle.elementSizeInBytes = sizeof(Index);

	m_IndexCache.emplace(filepath, handle);
	return handle;
}

Arc< Texture > SceneResource::GetOrLoadTexture(u32 entity, std::string_view filepath)
{
	auto& rm = m_RenderDevice.GetResourceManager();

	std::string f = filepath.data();
	if (m_TextureCache.contains(f))
	{
		return m_TextureCache.find(f)->second;
	}

	fs::path path = filepath;

	u32 width, height, numChannels;
	u8* pData = stbi_load(path.string().c_str(), (int*)&width, (int*)&height, (int*)&numChannels, STBI_rgb_alpha);
	BB_ASSERT(pData, "No texture found on the path: %s", path.string().c_str());

	Texture::CreationInfo texInfo = {};
	texInfo.resolution    = { width, height, 1 };
	texInfo.format        = VK_FORMAT_R8G8B8A8_UNORM;
	texInfo.bGenerateMips = false;
	texInfo.imageUsage    = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	auto pTex = Texture::Create(m_RenderDevice, path.filename().string(), 
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
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.mipLevel = 0,
		.baseArrayLayer = 0,
		.layerCount = 1
	};
	region.imageExtent       = { width, height, 1 };

	rm.UploadData(pTex, (void*)pData, texSizeInBytes, region);

	m_TextureCache.emplace(filepath, pTex);

	RELEASE(pData);
	return pTex;
}

void SceneResource::ResetFrameBuffers()
{
	m_pIndirectDrawBufferPool->Reset();
	m_pTransformBufferPool->Reset();
	m_pMaterialBufferPool->Reset();
}

void SceneResource::UpdateFrameBuffer(const void* pData, u32 count, u64 elementSizeInBytes, StaticBufferAllocator* pTargetBuffer)
{
	if (count == 0 || elementSizeInBytes == 0)
		return;

	auto& rm = m_RenderDevice.GetResourceManager();

	u64 sizeInBytes = count * elementSizeInBytes;

	auto allocation = pTargetBuffer->Allocate(count, elementSizeInBytes);
	rm.UploadData(allocation.vkBuffer, pData, sizeInBytes, VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, allocation.offset);
}

VkDescriptorBufferInfo SceneResource::GetVertexDescriptorInfo() const
{
	VkDescriptorBufferInfo descriptorInfo = {};
	descriptorInfo.buffer = m_pVertexBufferPool->vkBuffer();
	descriptorInfo.offset = 0;
	descriptorInfo.range  = m_pVertexBufferPool->GetAllocatedSize();
	return descriptorInfo;
}

VkDescriptorBufferInfo SceneResource::GetIndexDescriptorInfo() const
{
	VkDescriptorBufferInfo descriptorInfo = {};
	descriptorInfo.buffer = m_pIndexBufferPool->vkBuffer();
	descriptorInfo.offset = 0;
	descriptorInfo.range  = m_pIndexBufferPool->GetAllocatedSize();
	return descriptorInfo;
}

VkDescriptorBufferInfo SceneResource::GetIndirectDrawDescriptorInfo() const
{
	VkDescriptorBufferInfo descriptorInfo = {};
	descriptorInfo.buffer = m_pIndirectDrawBufferPool->vkBuffer();
	descriptorInfo.offset = 0;
	descriptorInfo.range  = m_pIndirectDrawBufferPool->GetAllocatedSize();
	return descriptorInfo;
}

VkDescriptorBufferInfo SceneResource::GetTransformDescriptorInfo() const
{
	VkDescriptorBufferInfo descriptorInfo = {};
	descriptorInfo.buffer = m_pTransformBufferPool->vkBuffer();
	descriptorInfo.offset = 0;
	descriptorInfo.range  = m_pTransformBufferPool->GetAllocatedSize();
	return descriptorInfo;
}

VkDescriptorBufferInfo SceneResource::GetMaterialDescriptorInfo() const
{
	VkDescriptorBufferInfo descriptorInfo = {};
	descriptorInfo.buffer = m_pMaterialBufferPool->vkBuffer();
	descriptorInfo.offset = 0;
	descriptorInfo.range  = m_pMaterialBufferPool->GetAllocatedSize();
	return descriptorInfo;
}

VkDescriptorSet SceneResource::GetSceneDescriptorSet() const
{
	return m_pDescriptorPool->AllocateSet(m_vkSetLayout).vkDescriptorSet();
}

} // namespace vk