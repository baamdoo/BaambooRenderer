#include "RenderResources.h"
#include "RenderDevice.h"

namespace render
{

//-------------------------------------------------------------------------
// Buffer
//-------------------------------------------------------------------------
Arc< Buffer > Buffer::Create(RenderDevice& rd, const std::string& name, CreationInfo&& desc)
{
	return rd.CreateBuffer(name, std::move(desc));
}

Arc< Buffer > Buffer::CreateEmpty(RenderDevice& rd, const std::string& name)
{
	return rd.CreateEmptyBuffer(name);
}

Buffer::Buffer(const std::string& name)
	: Super(name, eResourceType::Buffer)
{
}

Buffer::Buffer(const std::string& name, CreationInfo&& desc)
	: Super(name, eResourceType::Buffer)
	, m_CreationInfo(std::move(desc))
{
}


//-------------------------------------------------------------------------
// Texture
//-------------------------------------------------------------------------
Arc< Texture > Texture::Create(RenderDevice& rd, const std::string& name, CreationInfo&& desc)
{
	return rd.CreateTexture(name, std::move(desc));
}

Arc< Texture > Texture::CreateEmpty(RenderDevice& rd, const std::string& name)
{
	return rd.CreateEmptyTexture(name);
}

Texture::Texture(const std::string& name)
	: Super(name, eResourceType::Texture)
{
}

Texture::Texture(const std::string& name, CreationInfo&& info)
	: Super(name, eResourceType::Texture)
	, m_CreationInfo(info)
{
}

bool Texture::IsDepthTexture() const
{
	return m_CreationInfo.format >= eFormat::D32_FLOAT;
}


//-------------------------------------------------------------------------
// Sampler
//-------------------------------------------------------------------------
Arc< Sampler > Sampler::Create(RenderDevice& rd, const std::string& name, CreationInfo&& info)
{
	return rd.CreateSampler(name, std::move(info));
}

Arc< Sampler > Sampler::CreateLinearRepeat(RenderDevice& rd, const std::string& name) {
	return Create(rd, name,
		{
			.filter      = eFilterMode::Linear,
			.addressMode = eAddressMode::Wrap
		});
}

Arc< Sampler > Sampler::CreateLinearClamp(RenderDevice& rd, const std::string& name)
{
	return Create(rd, name,
		{
			.filter      = eFilterMode::Linear,
			.addressMode = eAddressMode::ClampEdge
		});
}

Arc< Sampler > Sampler::CreatePointRepeat(RenderDevice& rd, const std::string& name)
{
	return Create(rd, name,
		{
			.filter        = eFilterMode::Point,
			.addressMode   = eAddressMode::Wrap,
			.maxAnisotropy = 0.0f
		});
}

Arc< Sampler > Sampler::CreatePointClamp(RenderDevice& rd, const std::string& name)
{
	return Create(rd, name,
		{
			.filter        = eFilterMode::Point,
			.addressMode   = eAddressMode::ClampEdge,
			.maxAnisotropy = 0.0f
		});
}

Arc< Sampler > Sampler::CreateLinearClampCmp(RenderDevice& rd, const std::string& name)
{
	return Create(rd, name,
		{
			.filter        = eFilterMode::Linear,
			.addressMode   = eAddressMode::ClampBorder,
			.maxAnisotropy = 0.0f,
			.compareOp     = eCompareOp::LessEqual,
			.borderColor   = eBorderColor::OpaqueWhite_Float
		});
}

Sampler::Sampler(const std::string& name, CreationInfo&& info)
	: Super(name, eResourceType::Sampler)
	, m_CreationInfo(std::move(info))
{
}


//-------------------------------------------------------------------------
// Render Target
//-------------------------------------------------------------------------
Arc< RenderTarget > RenderTarget::CreateEmpty(RenderDevice& rd, const std::string& name)
{
	return rd.CreateEmptyRenderTarget(name);
}

RenderTarget::RenderTarget(const std::string& name)
	: Super(name, eResourceType::RenderTarget)
	, m_pAttachments(eAttachmentPoint::NumAttachmentPoints)
{
}

RenderTarget& RenderTarget::AttachTexture(eAttachmentPoint attachmentPoint, Arc< Texture > tex)
{
	assert(attachmentPoint < eAttachmentPoint::NumAttachmentPoints);
	m_pAttachments[attachmentPoint] = tex;

	return *this;
}
RenderTarget& RenderTarget::SetLoadAttachment(eAttachmentPoint attachmentPoint)
{
	m_bLoadAttachmentBits |= (1 << attachmentPoint);
	return *this;
}


//-------------------------------------------------------------------------
// Shader
//-------------------------------------------------------------------------
Arc< Shader > Shader::Create(RenderDevice& rd, const std::string& name, CreationInfo&& info)
{
	return rd.CreateShader(name, std::move(info));
}

Shader::Shader(const std::string& name, CreationInfo&& info)
	: Super(name, eResourceType::Shader)
	, m_CreationInfo(info)
{
}


//-------------------------------------------------------------------------
// Pipelines
//-------------------------------------------------------------------------
Box< GraphicsPipeline > GraphicsPipeline::Create(RenderDevice& rd, const std::string& name)
{
	return rd.CreateGraphicsPipeline(name);
}

GraphicsPipeline::GraphicsPipeline(const std::string& name)
	: m_Name(name.c_str())
{
}

GraphicsPipeline& GraphicsPipeline::SetShaders(
	Arc< Shader > pVS,
	Arc< Shader > pPS,
	Arc< Shader > pGS,
	Arc< Shader > pHS,
	Arc< Shader > pDS)
{
	m_pVS = pVS;
	m_pPS = pPS;
	m_pGS = pGS;
	m_pHS = pHS;
	m_pDS = pDS;

	m_bMeshShader = false;

	return *this;
}
GraphicsPipeline& GraphicsPipeline::SetMeshShaders(
	Arc< Shader > pMS,
	Arc< Shader > pTS)
{
	m_pMS = pMS;
	m_pTS = pTS;

	m_bMeshShader = true;

	return *this;
}

std::pair< u32, u32 > GraphicsPipeline::GetResourceBindingIndex(const std::string& name)
{
	auto iter = m_ResourceBindingMap.find(name);
	if (iter == m_ResourceBindingMap.end())
		return { INVALID_INDEX, INVALID_INDEX };

	return { (u32)(iter->second >> 32), (u32)(iter->second & 0xFFFFFFFF) };
}


Box< ComputePipeline > ComputePipeline::Create(RenderDevice& rd, const std::string& name)
{
	return rd.CreateComputePipeline(name);
}

ComputePipeline::ComputePipeline(const std::string& name)
	: m_Name(name.c_str())
{
}

ComputePipeline& ComputePipeline::SetComputeShader(Arc< Shader > pCS)
{
	m_pCS = pCS;
	return *this;
}

std::pair< u32, u32 > ComputePipeline::GetResourceBindingIndex(const std::string& name)
{
	auto iter = m_ResourceBindingMap.find(name);
	if (iter == m_ResourceBindingMap.end())
		return { -1, -1 };

	return { (u32)(iter->second >> 32), (u32)(iter->second & 0xFFFFFFFF) };
}


//-------------------------------------------------------------------------
// Resource Manager
//-------------------------------------------------------------------------
ResourceManager::~ResourceManager()
{
	RELEASE(m_pSceneResource);
}

//void ResourceManager::SetBuffer(const std::string& name, Weak< Buffer > buffer)
//{
//	m_Buffers.emplace(name, buffer);
//}
//
//void ResourceManager::SetTexture(const std::string& name, Weak< Texture > texture)
//{
//	m_Textures.emplace(name, texture);
//}
//
//void ResourceManager::SetSampler(const std::string& name, Arc< Sampler > sampler)
//{
//	m_Samplers.emplace(name, sampler);
//}
//
//Weak< Buffer > ResourceManager::GetBuffer(const std::string& name) const
//{
//	auto iter = m_Buffers.find(name);
//	if (iter == m_Buffers.end())
//		return nullptr;
//
//	return iter->second;
//}
//
//Weak< Texture > ResourceManager::GetTexture(const std::string& name) const
//{
//	auto iter = m_Textures.find(name);
//	if (iter == m_Textures.end())
//		return nullptr;
//
//	return iter->second;
//}
//
//Arc< Sampler > ResourceManager::GetSampler(const std::string& name) const
//{
//	auto iter = m_Samplers.find(name);
//	if (iter == m_Samplers.end())
//		return nullptr;
//
//	return iter->second;
//}

} // namespace render