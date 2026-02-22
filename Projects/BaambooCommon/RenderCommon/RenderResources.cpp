#include "RenderResources.h"
#include "RenderDevice.h"

namespace render
{

//-------------------------------------------------------------------------
// Buffer
//-------------------------------------------------------------------------
Arc< Buffer > Buffer::Create(RenderDevice& rd, const char* name, CreationInfo&& desc)
{
	return rd.CreateBuffer(name, std::move(desc));
}

Arc< Buffer > Buffer::CreateEmpty(RenderDevice& rd, const char* name)
{
	return rd.CreateEmptyBuffer(name);
}

Buffer::Buffer(const char* name)
	: Super(name, eResourceType::Buffer)
{
}

Buffer::Buffer(const char* name, CreationInfo&& desc)
	: Super(name, eResourceType::Buffer)
	, m_CreationInfo(std::move(desc))
{
}


//-------------------------------------------------------------------------
// Texture
//-------------------------------------------------------------------------
Arc< Texture > Texture::Create(RenderDevice& rd, const char* name, CreationInfo&& desc)
{
	return rd.CreateTexture(name, std::move(desc));
}

Arc< Texture > Texture::CreateEmpty(RenderDevice& rd, const char* name)
{
	return rd.CreateEmptyTexture(name);
}

Texture::Texture(const char* name)
	: Super(name, eResourceType::Texture)
{
}

Texture::Texture(const char* name, CreationInfo&& info)
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
Arc< Sampler > Sampler::Create(RenderDevice& rd, const char* name, CreationInfo&& info)
{
	return rd.CreateSampler(name, std::move(info));
}

Arc< Sampler > Sampler::CreateLinearRepeat(RenderDevice& rd, const char* name) {
	return Create(rd, name,
		{
			.filter      = eFilterMode::Linear,
			.addressMode = eAddressMode::Wrap
		});
}

Arc< Sampler > Sampler::CreateLinearClamp(RenderDevice& rd, const char* name)
{
	return Create(rd, name,
		{
			.filter      = eFilterMode::Linear,
			.addressMode = eAddressMode::ClampEdge
		});
}

Arc< Sampler > Sampler::CreatePointRepeat(RenderDevice& rd, const char* name)
{
	return Create(rd, name,
		{
			.filter        = eFilterMode::Point,
			.addressMode   = eAddressMode::Wrap,
			.maxAnisotropy = 0.0f
		});
}

Arc< Sampler > Sampler::CreatePointClamp(RenderDevice& rd, const char* name)
{
	return Create(rd, name,
		{
			.filter        = eFilterMode::Point,
			.addressMode   = eAddressMode::ClampEdge,
			.maxAnisotropy = 0.0f
		});
}

Arc< Sampler > Sampler::CreateLinearClampCmp(RenderDevice& rd, const char* name)
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

Sampler::Sampler(const char* name, CreationInfo&& info)
	: Super(name, eResourceType::Sampler)
	, m_CreationInfo(std::move(info))
{
}


//-------------------------------------------------------------------------
// Render Target
//-------------------------------------------------------------------------
Arc< RenderTarget > RenderTarget::CreateEmpty(RenderDevice& rd, const char* name)
{
	return rd.CreateEmptyRenderTarget(name);
}

RenderTarget::RenderTarget(const char* name)
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
Arc< Shader > Shader::Create(RenderDevice& rd, const char* name, CreationInfo&& info)
{
	return rd.CreateShader(name, std::move(info));
}

Shader::Shader(const char* name, CreationInfo&& info)
	: Super(name, eResourceType::Shader)
	, m_CreationInfo(info)
{
}


//-------------------------------------------------------------------------
// SBT
//-------------------------------------------------------------------------
Arc< ShaderBindingTable > ShaderBindingTable::Create(RenderDevice& rd, const char* name)
{
	return rd.CreateSBT(name);
}

ShaderBindingTable::ShaderBindingTable(const char* name)
	: Super(name, eResourceType::SBT)
{
}

ShaderBindingTable& ShaderBindingTable::SetRayGenerationRecord(const void* pIdentifier, const void* pData, u32 sizeInBytes)
{
	m_RayGenRecord.pIdentifier = pIdentifier;
	m_RayGenRecord.Upload(pData, sizeInBytes);
	return *this;
}

ShaderBindingTable& ShaderBindingTable::AddMissRecord(const std::string& missExportName, const void* pIdentifier, const void* pData, u32 localArgsSize)
{
	ShaderRecord record;
	record.exportName  = missExportName;
	record.pIdentifier = pIdentifier;
	record.Upload(pData, localArgsSize);
	m_MissRecords.push_back(std::move(record));

	return *this;
}

ShaderBindingTable& ShaderBindingTable::AddHitGroupRecord(const std::string& hitGroupName, const void* pIdentifier, const void* pData, u32 localArgsSize)
{
	ShaderRecord record;
	record.exportName  = hitGroupName;
	record.pIdentifier = pIdentifier;
	record.Upload(pData, localArgsSize);
	m_HitGroupRecords.push_back(std::move(record));

	return *this;
}

ShaderBindingTable& ShaderBindingTable::UpdateHitGroupLocalRootArguments(u32 recordIndex, const void* pIdentifier, const void* pData, u32 sizeInBytes)
{
	assert(recordIndex < m_HitGroupRecords.size() && "recordIndex out of range!");

	m_HitGroupRecords[recordIndex].Upload(pData, sizeInBytes);
	return *this;
}

void ShaderBindingTable::Reset()
{
	m_RayGenRecord = {};
	m_MissRecords.clear();
	m_HitGroupRecords.clear();
}


//-------------------------------------------------------------------------
// Acceleration Structure
//-------------------------------------------------------------------------
Arc< BottomLevelAccelerationStructure > BottomLevelAccelerationStructure::Create(RenderDevice& rd, const char* name)
{
	return rd.CreateBLAS(name);
}

BottomLevelAccelerationStructure::BottomLevelAccelerationStructure(const char* name)
	: Super(name, eResourceType::BLAS)
{
}

BottomLevelAccelerationStructure& BottomLevelAccelerationStructure::AddGeometry(const GeometryDesc& geometry)
{
	m_Geometries.push_back(geometry);
	return *this;
}

BottomLevelAccelerationStructure& BottomLevelAccelerationStructure::SetBuildFlags(RenderFlags flags)
{
	m_BuildFlags = flags;
	return *this;
}

Arc< TopLevelAccelerationStructure > TopLevelAccelerationStructure::Create(RenderDevice& rd, const char* name)
{
	return rd.CreateTLAS(name);
}

TopLevelAccelerationStructure::TopLevelAccelerationStructure(const char* name)
	: Super(name, eResourceType::TLAS)
{
}

TopLevelAccelerationStructure& TopLevelAccelerationStructure::AddInstance(const AccelerationStructureInstanceDesc& instance)
{
	m_Instances.push_back(instance);
	return *this;
}

TopLevelAccelerationStructure& TopLevelAccelerationStructure::SetBuildFlags(RenderFlags flags)
{
	m_BuildFlags = flags;
	return *this;
}

void TopLevelAccelerationStructure::Reset()
{
	m_Instances.clear();
}


//-------------------------------------------------------------------------
// Graphics Pipeline
//-------------------------------------------------------------------------
Box< GraphicsPipeline > GraphicsPipeline::Create(RenderDevice& rd, const char* name)
{
	return rd.CreateGraphicsPipeline(name);
}

GraphicsPipeline::GraphicsPipeline(const char* name)
	: m_Name(name)
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
	Arc< Shader > pPS,
	Arc< Shader > pTS)
{
	m_pTS = pTS;
	m_pMS = pMS;
	m_pPS = pPS;

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


//-------------------------------------------------------------------------
// Compute Pipeline
//-------------------------------------------------------------------------
Box< ComputePipeline > ComputePipeline::Create(RenderDevice& rd, const char* name)
{
	return rd.CreateComputePipeline(name);
}

ComputePipeline::ComputePipeline(const char* name)
	: m_Name(name)
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
// DXR Pipeline
//-------------------------------------------------------------------------
Box< RaytracingPipeline > RaytracingPipeline::Create(RenderDevice& rd, const char* name)
{
	return rd.CreateRaytracingPipeline(name);
}

RaytracingPipeline::RaytracingPipeline(const char* name)
	: m_Name(name)
{
}

RaytracingPipeline& RaytracingPipeline::SetShaderLibrary(Arc<Shader> pLibrary)
{
	m_pShaderLibrary = pLibrary;
	return *this;
}

RaytracingPipeline& RaytracingPipeline::SetRayGenerationShader(const std::string& exportName)
{
	m_RayGenExport = exportName;
	return *this;
}

RaytracingPipeline& RaytracingPipeline::AddMissShader(const std::string& exportName)
{
	m_MissExports.push_back(exportName);
	return *this;
}

RaytracingPipeline& RaytracingPipeline::AddHitGroup(const RaytracingHitGroup& hitGroup)
{
	m_HitGroups.push_back(hitGroup);
	return *this;
}

RaytracingPipeline& RaytracingPipeline::SetMaxPayloadSize(u32 sizeInBytes)
{
	m_MaxPayloadSizeInBytes = sizeInBytes;
	return *this;
}

RaytracingPipeline& RaytracingPipeline::SetMaxAttributeSize(u32 sizeInBytes)
{
	m_MaxAttributeSizeInBytes = sizeInBytes;
	return *this;
}

RaytracingPipeline& RaytracingPipeline::SetMaxRecursionDepth(u32 depth)
{
	m_MaxRecursionDepth = depth;
	return *this;
}

std::pair< u32, u32 > RaytracingPipeline::GetResourceBindingIndex(const std::string& name)
{
	auto iter = m_ResourceBindingMap.find(name);
	if (iter == m_ResourceBindingMap.end())
		return { INVALID_INDEX, INVALID_INDEX };

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