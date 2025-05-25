#include "RendererPch.h"
#include "VkResourceManager.h"
#include "VkCommandQueue.h"
#include "VkCommandBuffer.h"
#include "VkDescriptorSet.h"
#include "VkDescriptorPool.h"
#include "VkBufferAllocator.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>
#include <BaambooUtils/Math.hpp>

namespace vk
{

//-------------------------------------------------------------------------
// Scene Resource
//-------------------------------------------------------------------------
SceneResource::SceneResource(RenderContext& context)
	: m_renderContext(context)
{
	// **
	// scene buffers
	// **
	m_pVertexBufferPool = new StaticBufferAllocator(m_renderContext, sizeof(Vertex) * _1MB);
	m_pIndexBufferPool = new StaticBufferAllocator(m_renderContext, sizeof(Index) * 3 * _1MB, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
	m_pIndirectDrawBufferPool = new StaticBufferAllocator(m_renderContext, sizeof(IndirectDrawData) * _1MB, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
	m_pTransformBufferPool = new StaticBufferAllocator(m_renderContext, sizeof(TransformData) * _1MB);
	m_pMaterialBufferPool = new StaticBufferAllocator(m_renderContext, sizeof(MaterialData) * _1MB);

	// **
	// scene descriptor pool
	// **
	std::vector< VkDescriptorPoolSize > poolSizes = 
	{
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 8 },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAX_BINDLESS_DESCRIPTOR_RESOURCE_COUNT },
	};
	m_pDescriptorPool = new DescriptorPool(m_renderContext, std::move(poolSizes), 1, VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT);

	std::vector< VkDescriptorSetLayoutBinding > bindings = 
	{
		{ eStaticSetBindingIndex_CombinedImage2D, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAX_BINDLESS_DESCRIPTOR_RESOURCE_COUNT, VK_SHADER_STAGE_FRAGMENT_BIT },
		{ eStaticSetBindingIndex_Vertex, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT },
		{ eStaticSetBindingIndex_Index, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT },
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
		VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT_EXT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT_EXT,
	};

	VkDescriptorSetLayoutBindingFlagsCreateInfoEXT bindingFlagsInfo = {};
	bindingFlagsInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO_EXT;
	bindingFlagsInfo.bindingCount = static_cast<u32>(flags.size());
	bindingFlagsInfo.pBindingFlags = flags.data();

	VkDescriptorSetLayoutCreateInfo layoutInfo = {};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.pNext = &bindingFlagsInfo;
	layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT_EXT;
	layoutInfo.bindingCount = static_cast<u32>(bindings.size());
	layoutInfo.pBindings = bindings.data();
	VK_CHECK(vkCreateDescriptorSetLayout(m_renderContext.vkDevice(), &layoutInfo, nullptr, &m_vkSetLayout));


	// **
	// default sampler for scene textures
	// **
	auto& rm = m_renderContext.GetResourceManager();
	Sampler::CreationInfo samplerInfo = {};
	samplerInfo.type = eSamplerType::Repeat;
	samplerInfo.interpolation = eSamplerInterpolation::Linear;
	samplerInfo.mipLodBias = 0.0f;
	samplerInfo.maxAnisotropy = m_renderContext.DeviceProps().limits.maxSamplerAnisotropy;
	samplerInfo.lod = FLT_MAX;
	samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
	m_defaultSampler = rm.Create< Sampler >(L"DefaultSampler", std::move(samplerInfo));
}

SceneResource::~SceneResource()
{
	vkDestroyDescriptorSetLayout(m_renderContext.vkDevice(), m_vkSetLayout, nullptr);
	RELEASE(m_pDescriptorPool);

	RELEASE(m_pMaterialBufferPool);
	RELEASE(m_pTransformBufferPool);
	RELEASE(m_pIndirectDrawBufferPool);
	RELEASE(m_pIndexBufferPool);
	RELEASE(m_pVertexBufferPool);
}

void SceneResource::UpdateSceneResources(const SceneRenderView& sceneView)
{
	auto& rm = m_renderContext.GetResourceManager();

	ResetFrameBuffers();
	auto pDefaultSampler = rm.Get(m_defaultSampler);

	std::vector< TransformData > transforms;
	transforms.reserve(sceneView.transforms.size());
	for (auto& transformView : sceneView.transforms)
	{
		TransformData transform = {};
		transform.mWorld = transformView.mWorld;
		transforms.push_back(transform);
	}
	UpdateFrameBuffer(transforms.data(), (u32)transforms.size(), sizeof(TransformData), m_pTransformBufferPool);

	std::vector< MaterialData > materials;
	materials.reserve(sceneView.materials.size());
	std::vector< VkDescriptorImageInfo > imageInfos;
	for (auto& materialView : sceneView.materials)
	{
		MaterialData material = {};
		material.tint = materialView.tint;
		material.roughness = materialView.roughness;
		material.metallic = materialView.metallic;

		material.albedoID = INVALID_INDEX;
		if (!materialView.albedoTex.empty())
		{
			auto albedo = GetOrLoadTexture(materialView.id, materialView.albedoTex);
			imageInfos.push_back({ pDefaultSampler->vkSampler(), rm.Get(albedo)->vkView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
			material.albedoID = (u32)imageInfos.size() - 1;
		}

		material.normalID = INVALID_INDEX;
		if (!materialView.normalTex.empty())
		{
			auto normal = GetOrLoadTexture(materialView.id, materialView.normalTex);
			imageInfos.push_back({ pDefaultSampler->vkSampler(), rm.Get(normal)->vkView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
			material.normalID = (u32)imageInfos.size() - 1;
		}

		material.specularID = INVALID_INDEX;
		if (!materialView.specularTex.empty())
		{
			auto specular = GetOrLoadTexture(materialView.id, materialView.specularTex);
			imageInfos.push_back({ pDefaultSampler->vkSampler(), rm.Get(specular)->vkView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
			material.specularID = (u32)imageInfos.size() - 1;
		}

		material.aoID = INVALID_INDEX;
		if (!materialView.aoTex.empty())
		{
			auto ao = GetOrLoadTexture(materialView.id, materialView.aoTex);
			imageInfos.push_back({ pDefaultSampler->vkSampler(), rm.Get(ao)->vkView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
			material.aoID = (u32)imageInfos.size() - 1;
		}

		material.roughnessID = INVALID_INDEX;
		if (!materialView.roughnessTex.empty())
		{
			auto roughness = GetOrLoadTexture(materialView.id, materialView.roughnessTex);
			imageInfos.push_back({ pDefaultSampler->vkSampler(), rm.Get(roughness)->vkView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
			material.roughnessID = (u32)imageInfos.size() - 1;
		}

		material.metallicID = INVALID_INDEX;
		if (!materialView.metallicTex.empty())
		{
			auto metallic = GetOrLoadTexture(materialView.id, materialView.metallicTex);
			imageInfos.push_back({ pDefaultSampler->vkSampler(), rm.Get(metallic)->vkView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
			material.metallicID = (u32)imageInfos.size() - 1;
		}

		material.emissionID = INVALID_INDEX;
		if (!materialView.emissionTex.empty())
		{
			auto emission = GetOrLoadTexture(materialView.id, materialView.emissionTex);
			imageInfos.push_back({ pDefaultSampler->vkSampler(), rm.Get(emission)->vkView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
			material.emissionID = (u32)imageInfos.size() - 1;
		}

		materials.push_back(material);
	}
	UpdateFrameBuffer(materials.data(), (u32)materials.size(), sizeof(MaterialData), m_pMaterialBufferPool);

	std::vector< IndirectDrawData > draws;
	for (auto& [id, data] : sceneView.draws)
	{
		IndirectDrawData draw = {};
		if (IsValidIndex(data.mesh))
		{
			assert(data.mesh < sceneView.meshes.size());
			auto& meshView = sceneView.meshes[data.mesh];

			auto vertex = GetOrUpdateVertex(meshView.id, meshView.tag, meshView.vData, meshView.vCount);
			auto index = GetOrUpdateIndex(meshView.id, meshView.tag, meshView.iData, meshView.iCount);

			draw.indexCount = index.count;
			draw.instanceCount = 1;
			draw.firstIndex = index.offset;
			draw.vertexOffset = vertex.offset;
			draw.firstInstance = 0;

			draw.materialIndex = INVALID_INDEX;
			if (IsValidIndex(data.material))
			{
				assert(data.material < sceneView.materials.size());
				draw.materialIndex = data.material;
			}

			assert(IsValidIndex(data.transform) && data.transform < sceneView.transforms.size());
			draw.transformID = data.transform;
			draw.transformCount = (u32)transforms.size();
		}
		else
		{
			draw.instanceCount = 0;
		}

		draws.push_back(draw);
	}
	UpdateFrameBuffer(draws.data(), (u32)draws.size(), sizeof(IndirectDrawData), m_pIndirectDrawBufferPool);

	auto& descriptorSet = m_pDescriptorPool->AllocateSet(m_vkSetLayout);
	descriptorSet.StageDescriptors(imageInfos, eStaticSetBindingIndex_CombinedImage2D, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
	descriptorSet.StageDescriptor(GetVertexDescriptorInfo(), eStaticSetBindingIndex_Vertex, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
	descriptorSet.StageDescriptor(GetIndexDescriptorInfo(), eStaticSetBindingIndex_Index, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
	descriptorSet.StageDescriptor(GetIndirectDrawDescriptorInfo(), eStaticSetBindingIndex_IndirectDraw, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
	descriptorSet.StageDescriptor(GetTransformDescriptorInfo(), eStaticSetBindingIndex_Transform, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
	descriptorSet.StageDescriptor(GetMaterialDescriptorInfo(), eStaticSetBindingIndex_Material, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
}

BufferHandle SceneResource::GetOrUpdateVertex(u32 entity, std::string_view filepath, const void* pData, u32 count)
{
	auto& rm = m_renderContext.GetResourceManager();

	std::string f = filepath.data();
	if (m_vertexCache.contains(f))
	{
		return m_vertexCache.find(f)->second;
	}

	u64 sizeInBytes = sizeof(Vertex) * count;

	// staging buffer
	auto pStagingBuffer = rm.GetStagingBuffer(count, sizeof(Vertex));
	memcpy(pStagingBuffer->MappedMemory(), pData, sizeInBytes);

	// vertex buffer
	auto allocation = m_pVertexBufferPool->Allocate(count, sizeof(Vertex));

	// copy
	auto& cmdBuffer = m_renderContext.GraphicsQueue().Allocate(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, true);
	cmdBuffer.CopyBuffer(allocation.vkBuffer, pStagingBuffer->vkBuffer(), allocation.size, VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, allocation.offset);
	cmdBuffer.Close();
	m_renderContext.GraphicsQueue().ExecuteCommandBuffer(cmdBuffer);

	RELEASE(pStagingBuffer);

	BufferHandle handle = {};
	handle.vkBuffer = allocation.vkBuffer;
	handle.offset = allocation.offset;
	handle.count = count;
	handle.elementSizeInBytes = sizeof(Vertex);

	m_vertexCache.emplace(filepath, handle);
	return handle;
}

BufferHandle SceneResource::GetOrUpdateIndex(u32 entity, std::string_view filepath, const void* pData, u32 count)
{
	auto& rm = m_renderContext.GetResourceManager();

	std::string f = filepath.data();
	if (m_indexCache.contains(f))
	{
		return m_indexCache.find(f)->second;
	}

	u64 sizeInBytes = sizeof(Index) * count;

	// staging buffer
	auto pStagingBuffer = rm.GetStagingBuffer(count, sizeof(Index));
	memcpy(pStagingBuffer->MappedMemory(), pData, sizeInBytes);

	// index buffer
	auto allocation = m_pIndexBufferPool->Allocate(count, sizeof(Index));

	// copy
	auto& cmdBuffer = m_renderContext.GraphicsQueue().Allocate(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, true);
	cmdBuffer.CopyBuffer(allocation.vkBuffer, pStagingBuffer->vkBuffer(), allocation.size, VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, allocation.offset);
	cmdBuffer.Close();
	m_renderContext.GraphicsQueue().ExecuteCommandBuffer(cmdBuffer);

	RELEASE(pStagingBuffer);

	BufferHandle handle = {};
	handle.vkBuffer = allocation.vkBuffer;
	handle.offset = allocation.offset;
	handle.count = count;
	handle.elementSizeInBytes = sizeof(Index);

	m_indexCache.emplace(filepath, handle);
	return handle;
}

TextureHandle SceneResource::GetOrLoadTexture(u32 entity, std::string_view filepath)
{
	auto& rm = m_renderContext.GetResourceManager();

	std::string f = filepath.data();
	if (m_textureCache.contains(f))
	{
		return m_textureCache.find(f)->second;
	}

	fs::path path = filepath;

	u32 width, height, numChannels;
	u8* pData = stbi_load(path.string().c_str(), (int*)&width, (int*)&height, (int*)&numChannels, STBI_rgb_alpha);
	BB_ASSERT(pData, "No texture found on the path: %s", path.string().c_str());

	Texture::CreationInfo texInfo = {};
	texInfo.resolution = { width, height, 1 };
	texInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
	texInfo.bGenerateMips = false;
	texInfo.imageUsage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	auto tex = rm.Create< Texture >(path.filename().wstring(), std::move(texInfo));

	// **
	// Copy data to staging buffer
	// **
	auto pTex = rm.Get(tex);
	auto texSize = pTex->SizeInBytes();

	auto pStagingBuffer = rm.GetStagingBuffer(1, texSize);
	memcpy(pStagingBuffer->MappedMemory(), pData, texSize);

	VkBufferImageCopy region = {};
	region.bufferOffset = 0;
	region.bufferRowLength = 0;
	region.bufferImageHeight = 0;
	region.imageSubresource = {
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.mipLevel = 0,
		.baseArrayLayer = 0,
		.layerCount = 1
	};
	region.imageExtent = { width, height, 1 };

	auto& cmdBuffer = m_renderContext.GraphicsQueue().Allocate(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, true);
	cmdBuffer.CopyBuffer(pTex, pStagingBuffer, { region });
	//if (bGenerateMips)
	//	cmdBuffer.GenerateMips(pTex);
	cmdBuffer.Close();
	m_renderContext.GraphicsQueue().ExecuteCommandBuffer(cmdBuffer);

	m_textureCache.emplace(filepath, tex);

	RELEASE(pData);
	RELEASE(pStagingBuffer);
	return tex;
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

	auto& rm = m_renderContext.GetResourceManager();
	u64 sizeInBytes = count * elementSizeInBytes;

	auto pStagingBuffer = rm.GetStagingBuffer(count, elementSizeInBytes);
	memcpy(pStagingBuffer->MappedMemory(), pData, sizeInBytes);

	auto allocation = pTargetBuffer->Allocate(count, elementSizeInBytes);
	auto& cmdBuffer = m_renderContext.GraphicsQueue().Allocate(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, true);
	cmdBuffer.CopyBuffer(allocation.vkBuffer, pStagingBuffer->vkBuffer(), allocation.size, VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, allocation.offset);
	cmdBuffer.Close();
	m_renderContext.GraphicsQueue().ExecuteCommandBuffer(cmdBuffer);

	RELEASE(pStagingBuffer);
}

VkDescriptorBufferInfo SceneResource::GetVertexDescriptorInfo() const
{
	VkDescriptorBufferInfo descriptorInfo = {};
	descriptorInfo.buffer = m_pVertexBufferPool->vkBuffer();
	descriptorInfo.offset = 0;
	descriptorInfo.range = m_pVertexBufferPool->GetAllocatedSize();
	return descriptorInfo;
}

VkDescriptorBufferInfo SceneResource::GetIndexDescriptorInfo() const
{
	VkDescriptorBufferInfo descriptorInfo = {};
	descriptorInfo.buffer = m_pIndexBufferPool->vkBuffer();
	descriptorInfo.offset = 0;
	descriptorInfo.range = m_pIndexBufferPool->GetAllocatedSize();
	return descriptorInfo;
}

VkDescriptorBufferInfo SceneResource::GetIndirectDrawDescriptorInfo() const
{
	VkDescriptorBufferInfo descriptorInfo = {};
	descriptorInfo.buffer = m_pIndirectDrawBufferPool->vkBuffer();
	descriptorInfo.offset = 0;
	descriptorInfo.range = m_pIndirectDrawBufferPool->GetAllocatedSize();
	return descriptorInfo;
}

VkDescriptorBufferInfo SceneResource::GetTransformDescriptorInfo() const
{
	VkDescriptorBufferInfo descriptorInfo = {};
	descriptorInfo.buffer = m_pTransformBufferPool->vkBuffer();
	descriptorInfo.offset = 0;
	descriptorInfo.range = m_pTransformBufferPool->GetAllocatedSize();
	return descriptorInfo;
}

VkDescriptorBufferInfo SceneResource::GetMaterialDescriptorInfo() const
{
	VkDescriptorBufferInfo descriptorInfo = {};
	descriptorInfo.buffer = m_pMaterialBufferPool->vkBuffer();
	descriptorInfo.offset = 0;
	descriptorInfo.range = m_pMaterialBufferPool->GetAllocatedSize();
	return descriptorInfo;
}

VkDescriptorSet SceneResource::GetSceneDescriptorSet() const
{
	return m_pDescriptorPool->AllocateSet(m_vkSetLayout).vkDescriptorSet();
}


//-------------------------------------------------------------------------
// Resource Manager
//-------------------------------------------------------------------------
ResourceManager::ResourceManager(RenderContext& context)
	: m_renderContext(context)
{
}

ResourceManager::~ResourceManager()
{
}

Buffer* ResourceManager::GetStagingBuffer(u32 numElements, u64 elementSize) const
{
	Buffer::CreationInfo bufferInfo = {};
	bufferInfo.count = numElements;
	bufferInfo.elementSize = elementSize;
	bufferInfo.bMap = true;
	bufferInfo.bufferUsage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	return new Buffer(m_renderContext, L"StagingBuffer", std::move(bufferInfo));
}

} // namespace vk