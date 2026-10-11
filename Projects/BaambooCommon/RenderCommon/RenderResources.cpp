#include "RenderResources.h"
#include "RenderDevice.h"
#include "SceneRenderView.h"

#include <cmath>
#include <limits>

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

Arc< Sampler > Sampler::CreatePointClampMin(RenderDevice& rd, const char* name)
{
	return Create(rd, name,
		{
			.filter        = eFilterMode::Point,
			.mipmapMode    = eMipmapMode::Nearest,
			.addressMode   = eAddressMode::ClampEdge,
			.maxAnisotropy = 0.0f,
			.reductionMode = eReductionMode::Min
		});
}

Arc< Sampler > Sampler::CreateLinearClampMin(RenderDevice& rd, const char* name)
{
	return Create(rd, name,
		{
			.filter        = eFilterMode::Linear,
			.mipmapMode    = eMipmapMode::Nearest,
			.addressMode   = eAddressMode::ClampEdge,
			.maxAnisotropy = 0.0f,
			.reductionMode = eReductionMode::Min
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

void BottomLevelAccelerationStructure::Reset()
{
	m_Geometries.clear();
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
		return { kInvalidIndex, kInvalidIndex };

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
		return { kInvalidIndex, kInvalidIndex };

	return { (u32)(iter->second >> 32), (u32)(iter->second & 0xFFFFFFFF) };
}


//-------------------------------------------------------------------------
// Scene Resource
//-------------------------------------------------------------------------
// The project uses /fp:fast; preserve the outward-rounding operations below.
#ifdef _MSC_VER
#pragma float_control(precise, on, push)
#endif
SceneResource::MediumBuffers SceneResource::BuildMediumBuffers(const SceneRenderView& renderView)
{
    MediumBuffers buffers;
    buffers.media.resize(renderView.voxelTerrain.bValid ? kMaxVoxelChunkSlots : 0u);

    std::unordered_map< const MediumDensityAsset*, u32 > assetOffsets;
    for (const auto& [entity, draw] : renderView.draws)
    {
        UNUSED(entity);
        if (!IsValidIndex(draw.mesh))
            continue;

        MediumData data = {};
        if (IsValidIndex(draw.medium))
        {
            BB_ASSERT(draw.medium < renderView.media.size(), "Medium render-view index is out of range");
            BB_ASSERT(draw.transform < renderView.transforms.size(), "Medium transform index is out of range");

            const auto& medium = renderView.media[draw.medium];
            data = medium.data;

            data.mWorldToDensity    = renderView.transforms[draw.transform].mWorldInverse;
            data.densityByteOffset  = kInvalidIndex;

            if (medium.pDensity)
            {
                const auto* pAsset = medium.pDensity.get();
                data.mWorldToDensity = pAsset->mLocalToIndex * data.mWorldToDensity;
                auto assetIt = assetOffsets.find(pAsset);
                if (assetIt == assetOffsets.end())
                {
                    BB_ASSERT(!pAsset->words.empty() && pAsset->words.size() % 8u == 0u, "Density payload must have 32-byte alignment");

                    const u64 totalWords = static_cast<u64>(buffers.densityWords.size()) + pAsset->words.size();
                    BB_ASSERT(totalWords <= std::numeric_limits<u32>::max() / sizeof(u32),
                        "Density pool must fit its 32-bit byte offsets");

                    const u32 offset = static_cast<u32>(buffers.densityWords.size() * sizeof(u32));
                    buffers.densityWords.insert(buffers.densityWords.end(), pAsset->words.begin(), pAsset->words.end());
                    assetIt = assetOffsets.emplace(pAsset, offset).first;
                }
                data.densityByteOffset = assetIt->second;
            }

            const auto roundUp = [](double value)
            {
                float bound = static_cast<float>(value);
                if (static_cast<double>(bound) < value)
                    bound = std::nextafter(bound, std::numeric_limits<float>::infinity());

                BB_ASSERT(std::isfinite(bound), "Medium bound exceeds finite float32 range");
                return bound;
            };
            const float maxDensity = medium.pDensity ? medium.pDensity->maxDensity : 1.0f;

            const float densityBound = roundUp(static_cast<double>(maxDensity) * data.densityScale);
            data.majorant = 0.0f;
            for (u32 axis = 0; axis < 3; ++axis)
            {
                const float sigmaA = roundUp(static_cast<double>(data.sigmaA[axis]) * densityBound);
                const float sigmaS = roundUp(static_cast<double>(data.sigmaS[axis]) * densityBound);
                data.majorant = std::max(data.majorant, roundUp(static_cast<double>(sigmaA) + sigmaS));
            }

            const float determinant = glm::determinant(mat3(data.mWorldToDensity));
            BB_ASSERT(std::isfinite(determinant) && determinant != 0.0f &&
                data.mWorldToDensity[0][3] == 0.0f && data.mWorldToDensity[1][3] == 0.0f &&
                data.mWorldToDensity[2][3] == 0.0f && data.mWorldToDensity[3][3] == 1.0f,
                "Medium transform must be affine and invertible");
        }

        // Pure boundaries inherit the incident IOR during traversal
        data.ior = 1.0f;
        if (IsValidIndex(draw.material))
        {
            BB_ASSERT(draw.material < renderView.materials.size(), "Medium boundary material index is out of range");

            const auto& root = renderView.materials[draw.material];
            const bool bMediumBoundary = (root.materialFlags & MATERIAL_FLAG_MEDIUM_BOUNDARY) != 0u;

            u32 terminalMaterial = draw.material;
            if (root.layerCount > 1u)
            {
                const u64 terminal = static_cast<u64>(root.layerOffset) + root.layerCount - 1u;
                terminalMaterial = renderView.materialSlabs[terminal].materialID;
            }
            BB_ASSERT(terminalMaterial < renderView.materials.size(), "Medium terminal material is out of range");
            const float ior = renderView.materials[terminalMaterial].ior;
            if (!bMediumBoundary)
            {
                data.ior = std::max(ior, 1.0e-4f);
            }
        }
        buffers.media.push_back(data);
    }

    // Bind nonempty buffers even when the scene has no media or density grids.
    if (buffers.media.empty())
        buffers.media.emplace_back();
    if (buffers.densityWords.empty())
        buffers.densityWords.push_back(0u);
    return buffers;
}
#ifdef _MSC_VER
#pragma float_control(pop)
#endif


//-------------------------------------------------------------------------
// Resource Manager
//-------------------------------------------------------------------------
ResourceManager::~ResourceManager()
{
	RELEASE(m_pSceneResource);
}

void ResourceManager::ReplaceSceneResource(Box< SceneResource >&& pSceneResource)
{
	RELEASE(m_pSceneResource);
	m_pSceneResource = pSceneResource.release();
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
