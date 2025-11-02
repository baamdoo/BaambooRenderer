#pragma once
#include "RendererAPI.h"
#include "ShaderTypes.h"

#pragma warning(disable : 4251)

namespace render 
{

enum class eResourceType : u8
{
    Buffer,
    Texture,
    RenderTarget,
    Sampler,
    Shader,
    AccelerationStructure
};

class BAAMBOO_API Resource : public ArcBase
{
public:
    Resource(const std::string& name, eResourceType type)
        : m_Name(name.c_str()), m_Type(type) {}
    virtual ~Resource() = default;

protected:
    std::string   m_Name;
    eResourceType m_Type;
};


//-------------------------------------------------------------------------
// Buffer
//-------------------------------------------------------------------------
enum 
{
    eBufferUsage_TransferSource      = 0x00000001,
    eBufferUsage_TransferDest        = 0x00000002,
    eBufferUsage_UniformTexel        = 0x00000004,
    eBufferUsage_StorageTexel        = 0x00000008,
    eBufferUsage_Uniform             = 0x00000010,
    eBufferUsage_Storage             = 0x00000020,
    eBufferUsage_Index               = 0x00000040,
    eBufferUsage_Vertex              = 0x00000080,
    eBufferUsage_Indirect            = 0x00000100,
    eBufferUsage_ShaderDeviceAddress = 0x00020000,
};

class BAAMBOO_API Buffer : public Resource
{
using Super = Resource;
public:
    struct CreationInfo
    {
        u32  count              = 0;
        u64  elementSizeInBytes = 0;
        bool bMap               = false;

        RenderFlags bufferUsage = 0;
    };

    static Arc< Buffer > Create(RenderDevice& rd, const std::string& name, CreationInfo&& desc);
    static Arc< Buffer > CreateEmpty(RenderDevice& rd, const std::string& name);

    Buffer(const std::string& name);
    Buffer(const std::string& name, CreationInfo&& info);
    virtual ~Buffer() = default;

    virtual void Resize(u64 sizeInBytes, bool bReset = false) = 0;

    virtual u64 SizeInBytes() const { return 0; }

protected:
    CreationInfo m_CreationInfo = {};
};


//-------------------------------------------------------------------------
// Texture
//-------------------------------------------------------------------------
enum class eFormat
{
    UNKNOWN,

    RGBA32_FLOAT,
    RGBA32_UINT,
    RGBA32_SINT,
    RGB32_FLOAT,
    RGB32_UINT,
    RGB32_SINT,
    RG32_FLOAT,
    RG32_UINT,
    RG32_SINT,
    R32_FLOAT,
    R32_UINT,
    R32_SINT,

    RGBA16_FLOAT,
    RGBA16_UNORM,
    RGBA16_UINT,
    RGBA16_SNORM,
    RGBA16_SINT,
    RG16_FLOAT,
    RG16_UNORM,
    RG16_UINT,
    RG16_SNORM,
    RG16_SINT,
    R16_FLOAT,
    R16_UNORM,
    R16_UINT,
    R16_SNORM,
    R16_SINT,

    RGBA8_UNORM,
    RGBA8_SNORM,
    RGBA8_UINT,
    RGBA8_SINT,
    RGBA8_SRGB,
    RGB8_UNORM,
    RGB8_SNORM,
    RGB8_USCALED,
    RGB8_SSCALED,
    RGB8_UINT,
    RGB8_SINT,
    RGB8_SRGB,
    RG8_UNORM,
    RG8_SNORM,
    RG8_USCALED,
    RG8_SSCALED,
    RG8_UINT,
    RG8_SINT,
    RG8_SRGB,
    R8_UNORM,
    R8_UINT,
    R8_SNORM,
    R8_SINT,
    A8_UNORM,

    RG11B10_UFLOAT,

    D32_FLOAT,
    D24_UNORM_S8_UINT,
    D16_UNORM,
};

enum class eImageType
{
    Texture1D   = 0,
    Texture2D   = 1,
    Texture3D   = 2,
    TextureCube = 3,
};

enum 
{
    eTextureUsage_TransferSource         = 0x00000001,
    eTextureUsage_TransferDest           = 0x00000002,
    eTextureUsage_Sample                 = 0x00000004,
    eTextureUsage_Storage                = 0x00000008,
    eTextureUsage_ColorAttachment        = 0x00000010,
    eTextureUsage_DepthStencilAttachment = 0x00000020,
};

enum class eImageView
{
    None, // Vulkan
    ShaderResource,
    UnorderedAccess,
    RenderTarget,
    DepthStencil
};

enum class eTextureLayout
{
    Undefined              = 0,
    General                = 1,
    ColorAttachment        = 2,
    DepthStencilAttachment = 3,
    DepthStencilReadOnly   = 4,
    ShaderReadOnly         = 5,
    TransferSource         = 6,
    TransferDest           = 7,
    Present                = 8,
};

class BAAMBOO_API Texture : public Resource
{
using Super = Resource;
public:
    struct CreationInfo
    {
        eImageType     imageType     = eImageType::Texture2D;
        uint3          resolution    = {};
        eFormat        format        = eFormat::RGBA8_UNORM;
        RenderFlags    imageUsage    = 0;
        eTextureLayout initialLayout = eTextureLayout::Undefined;

        float clearValue[4]     = { 0, 0, 0, 0 };
        float depthClearValue   = 1.0f;
        u8    stencilClearValue = 0;

        u32  arrayLayers   = 1;
        u32  sampleCount   = 1;
        bool bFlipY        = false;
        bool bGenerateMips = false;
    };

    static Arc< Texture > Create(RenderDevice& rd, const std::string& name, CreationInfo&& desc);
    static Arc< Texture > CreateEmpty(RenderDevice& rd, const std::string& name);

    Texture(const std::string& name);
    Texture(const std::string& name, CreationInfo&& info);
    virtual ~Texture() = default;

    virtual void Resize(u32 width, u32 height, u32 depth) = 0;

    u32 Width() const { return m_CreationInfo.resolution.x; }
    u32 Height() const { return m_CreationInfo.resolution.y; }
    u32 Depth() const { return m_CreationInfo.resolution.z; }

    virtual bool IsDepthTexture() const;

protected:
    CreationInfo m_CreationInfo = {};
};


//-------------------------------------------------------------------------
// Sampler
//-------------------------------------------------------------------------
enum class eFilterMode
{
    Point,
    Linear,
    Anisotropic
};

enum class eMipmapMode
{
    Nearest,
    Linear
};

enum class eAddressMode 
{
    Wrap            = 0,
    MirrorRepeat    = 1,
    ClampEdge       = 2,
    ClampBorder     = 3,
    MirrorClampEdge = 4,
};

enum class eCompareOp
{
    Never        = 0,
    Less         = 1,
    Equal        = 2,
    LessEqual    = 3,
    Greater      = 4,
    NotEqual     = 5,
    GreaterEqual = 6,
    Always       = 7
};

enum class eBorderColor
{
    TransparentBlack_Float = 0,
    TransparentBlack_Int   = 1,
    OpaqueBlack_Float      = 2,
    OpaqueBlack_Int        = 3,
    OpaqueWhite_Float      = 4,
    OpaqueWhite_Int        = 5,
};

DLLEXPORT_TEMPLATE template class BAAMBOO_API Arc< Sampler >;
class BAAMBOO_API Sampler : public Resource
{
using Super = Resource;
public:
    struct CreationInfo
    {
        eFilterMode  filter        = eFilterMode::Linear;
        eMipmapMode  mipmapMode    = eMipmapMode::Linear;
        eAddressMode addressMode   = eAddressMode::Wrap;
        f32          mipLodBias    = 0.0f;
        f32          maxAnisotropy = 16.0f;
        eCompareOp   compareOp     = eCompareOp::Never;
        f32          minLod        = 0.0f;
        f32          maxLod        = LOD_CLAMP_NONE;
        eBorderColor borderColor   = eBorderColor::TransparentBlack_Float;
    };

    static Arc< Sampler > Create(RenderDevice& rd, const std::string& name, CreationInfo&& info);

    static Arc< Sampler > CreateLinearRepeat(RenderDevice& rd, const std::string& name = "LinearRepeat");
    static Arc< Sampler > CreateLinearClamp(RenderDevice& rd, const std::string& name = "LinearClamp");
    static Arc< Sampler > CreatePointRepeat(RenderDevice& rd, const std::string& name = "PointRepeat");
    static Arc< Sampler > CreatePointClamp(RenderDevice& rd, const std::string& name = "PointClamp");
    static Arc< Sampler > CreateLinearClampCmp(RenderDevice& rd, const std::string& name = "Shadow");

    Sampler(const std::string& name, CreationInfo&& info);
    virtual ~Sampler() = default;

protected:
    CreationInfo m_CreationInfo = {};
};


//-------------------------------------------------------------------------
// Render Target
//-------------------------------------------------------------------------
enum eAttachmentPoint 
{
    Color0 = 0,
    Color1,
    Color2,
    Color3,
    Color4,
    Color5,
    Color6,
    Color7,
    DepthStencil,

    All,
    NumColorAttachments = DepthStencil,
    NumAttachmentPoints = All,
};

DLLEXPORT_TEMPLATE template class BAAMBOO_API Arc< Texture >;
class BAAMBOO_API RenderTarget : public Resource
{
using Super = Resource;
public:
    static Arc< RenderTarget > CreateEmpty(RenderDevice& rd, const std::string& name);

    RenderTarget(const std::string& name);
    ~RenderTarget() = default;

    RenderTarget& AttachTexture(eAttachmentPoint attachmentPoint, Arc< Texture > tex);
    RenderTarget& SetLoadAttachment(eAttachmentPoint attachmentPoint);
    virtual void Build() = 0;

    virtual void Resize(u32 width, u32 height, u32 depth) = 0;
    virtual void Reset() = 0;
    virtual void InvalidateImageLayout() {}

    virtual Arc< Texture > Attachment(eAttachmentPoint attachment) const { return m_pAttachments[attachment]; }
    virtual const std::vector< Arc< Texture > >& GetAttachments() const { return m_pAttachments; }
    virtual u32 GetNumColors() const { return m_NumColors; }

protected:
    bool IsDepthOnly() const { return false; }

protected:
    std::vector< Arc< Texture > > m_pAttachments;

    u32 m_NumColors           = 0;
    u32 m_bLoadAttachmentBits = 0;
};


//-------------------------------------------------------------------------
// Shader
//-------------------------------------------------------------------------
enum eShaderStage
{
    // rasterize
    Vertex      = 0x00000001,
    Hull        = 0x00000002,
    Domain      = 0x00000004,
    Geometry    = 0x00000008,
    Fragment    = 0x00000010,
    Compute     = 0x00000020,
    AllGraphics = 0x0000001F,
    AllStage    = 0x7FFFFFFF,

    // ray-trace
    RayGeneration = 0x00000100,
    AnyHit        = 0x00000200,
    ClosestHit    = 0x00000400,
    Miss          = 0x00000800,
    Interaction   = 0x00001000,
    Callable      = 0x00002000,

    // mesh
    Task = 0x00000040,
    Mesh = 0x00000080,
};

enum class eShaderResourceType
{
    UniformBuffer = 0,
    StorageBuffer = 1,
    SampledImage  = 2,
    StorageImage  = 3,
    Sampler       = 4,

    Invalid = 0x7FFFFFFF,
};

struct ShaderBytecode 
{
    size_t      size  = 0;
    const void* pData = nullptr;
};

class BAAMBOO_API Shader : public Resource
{
using Super = Resource;

public:
    struct CreationInfo
    {
        eShaderStage stage;
        std::string  filename;
    };

    static Arc< Shader > Create(RenderDevice& rd, const std::string& name, CreationInfo&& info);

    Shader(const std::string& name, CreationInfo&& info);
    virtual ~Shader() = default;

    virtual ShaderBytecode ShaderModule() const { return m_ShaderBytecode; }

protected:
    CreationInfo   m_CreationInfo;
    ShaderBytecode m_ShaderBytecode;

    eShaderStage m_Stage;
};


//-------------------------------------------------------------------------
// Pipelines
//-------------------------------------------------------------------------
enum class ePrimitiveTopology
{
    Point     = 0,
    Line      = 1,
    Triangle  = 2,
    Patch     = 3,
};

enum class eCullMode
{
    None  = 0,
    Front = 1,
    Back  = 2,
};

enum class eFrontFace
{
    Clockwise        = 0,
    CounterClockwise = 1,
};

enum class eBlendFactor
{
    Zero             = 1,
    One              = 2,
    SrcColor         = 3,
    SrcColorInv      = 4,
    SrcAlpha         = 5,
    SrcAlphaInv      = 6,
    DstColor         = 7,
    DstColorInv      = 8,
    DstAlpha         = 9,
    DstAlphaInv      = 10,
    SrcAlphaSaturate = 11,
};

enum class eBlendOp
{
    Add         = 0,
    Subtract    = 1,
    SubtractInv = 2,
    Min         = 3,
    Max         = 4,
};

enum class eLogicOp
{
    None    = 0,
    Clear   = 1,
    Set     = 2,
    Copy    = 3,
    CopyInv = 4,
};

enum eComponentWriteMask
{
    R = (1 << 0),
    G = (1 << 1), 
    B = (1 << 2), 
    A = (1 << 3)
};

struct BlendDesc
{
    bool bBlendEnabled    = false;
    bool bLogicOpEnabled  = false;

    eBlendFactor srcBlend = eBlendFactor::One;
    eBlendFactor dstBlend = eBlendFactor::Zero;
    eBlendOp     blendOp  = eBlendOp::Add;

    eBlendFactor srcBlendAlpha = eBlendFactor::One;
    eBlendFactor dstBlendAlpha = eBlendFactor::Zero;
    eBlendOp     blendOpAlpha  = eBlendOp::Add;
};

DLLEXPORT_TEMPLATE template class BAAMBOO_API Arc< Shader >;
class BAAMBOO_API GraphicsPipeline : public ArcBase
{
public:
    static Box< GraphicsPipeline > Create(RenderDevice& device, const std::string& name);

    GraphicsPipeline(const std::string& name);
    virtual ~GraphicsPipeline() = default;

    GraphicsPipeline& SetShaders(
        Arc< Shader > pVS,
        Arc< Shader > pPS,
        Arc< Shader > pGS = Arc< Shader >(),
        Arc< Shader > pHS = Arc< Shader >(),
        Arc< Shader > pDS = Arc< Shader >());
    GraphicsPipeline& SetMeshShaders(
        Arc< Shader > pMS,
        Arc< Shader > pTS = Arc< Shader >());

    virtual GraphicsPipeline& SetRenderTarget(Arc< RenderTarget > renderTarget) = 0;

    virtual GraphicsPipeline& SetFillMode(bool bWireframe) = 0;
    virtual GraphicsPipeline& SetCullMode(eCullMode cullMode) = 0;

    virtual GraphicsPipeline& SetTopology(ePrimitiveTopology topology) = 0;
    virtual GraphicsPipeline& SetDepthTestEnable(bool bEnable, render::eCompareOp compareOp = render::eCompareOp::LessEqual) = 0;
    virtual GraphicsPipeline& SetDepthWriteEnable(bool bEnable, render::eCompareOp compareOp = render::eCompareOp::LessEqual) = 0;

    virtual GraphicsPipeline& SetLogicOp(eLogicOp logicOp) = 0;
    virtual GraphicsPipeline& SetBlendEnable(u32 renderTargetIndex, bool bEnable) = 0;
    virtual GraphicsPipeline& SetColorBlending(u32 renderTargetIndex, eBlendFactor srcBlend, eBlendFactor dstBlend, eBlendOp blendOp) = 0;
    virtual GraphicsPipeline& SetAlphaBlending(u32 renderTargetIndex, eBlendFactor srcBlend, eBlendFactor dstBlend, eBlendOp blendOp) = 0;

    virtual void Build() = 0;

    std::pair< u32, u32 > GetResourceBindingIndex(const std::string& name);

protected:
    std::string m_Name;

    Arc< Shader > m_pVS;
    Arc< Shader > m_pPS;
    Arc< Shader > m_pGS;
    Arc< Shader > m_pHS;
    Arc< Shader > m_pDS;

    Arc< Shader > m_pTS;
    Arc< Shader > m_pMS;

    bool m_bMeshShader = false;

    // [name, set:binding] - Vulkan
    // [name, offset:rootIndex] - Dx12
    std::unordered_map< std::string, u64 > m_ResourceBindingMap;
};

class BAAMBOO_API ComputePipeline : public ArcBase
{
public:
    static Box< ComputePipeline > Create(RenderDevice& device, const std::string& name);

    ComputePipeline(const std::string& name);
    virtual ~ComputePipeline() = default;

    ComputePipeline& SetComputeShader(Arc< Shader > pCS);
    //ComputePipeline& SetDescriptorLayout(Arc< DescriptorLayout > layout);

    virtual void Build() = 0;

    std::pair< u32, u32 > GetResourceBindingIndex(const std::string& name);

protected:
    std::string m_Name;

    Arc< Shader > m_pCS;

    // [name, set:binding] - Vulkan
    // [name, offset:rootIndex] - Dx12
    std::unordered_map< std::string, u64 > m_ResourceBindingMap;
};


//-------------------------------------------------------------------------
// Scene Resource
//-------------------------------------------------------------------------
class BAAMBOO_API SceneResource
{
public:
    virtual ~SceneResource() = default;

    virtual void UpdateSceneResources(const SceneRenderView& renderView) = 0;
    virtual void BindSceneResources(class CommandContext& context) = 0;
};


//-------------------------------------------------------------------------
// Resource Manager
//-------------------------------------------------------------------------
enum
{
    eDefaultTexture_White = 0,
    eDefaultTexture_Black = 1,
    eDefaultTexture_Gray  = 2,
};

class BAAMBOO_API ResourceManager
{
public:
    virtual ~ResourceManager();

    virtual Arc< Texture > LoadTexture(const std::string& filepath, bool bGenerateMips = false) = 0;

    virtual Arc< Texture > GetFlatWhiteTexture() { return m_pWhiteTexture; }
    virtual Arc< Texture > GetFlatBlackTexture() { return m_pBlackTexture; }
    virtual Arc< Texture > GetFlatGrayTexture() { return m_pGrayTexture; }

    virtual Arc< Texture > GetFlatWhiteTexture3D() { return m_pWhiteTexture3D; }
    virtual Arc< Texture > GetFlatBlackTexture3D() { return m_pBlackTexture3D; }

    /*void SetBuffer(const std::string& name, Weak< Buffer > buffer);
    void SetTexture(const std::string& name, Weak< Texture > texture);
    void SetSampler(const std::string& name, Arc< Sampler > sampler);

    Weak< Buffer > GetBuffer(const std::string& name) const;
    Weak< Texture > GetTexture(const std::string& name) const;
    Arc< Sampler > GetSampler(const std::string& name) const;*/

    virtual SceneResource& GetSceneResource() { return *m_pSceneResource; }

protected:
    Arc< Texture > m_pWhiteTexture;
    Arc< Texture > m_pBlackTexture;
    Arc< Texture > m_pGrayTexture;

    Arc< Texture > m_pWhiteTexture3D;
    Arc< Texture > m_pBlackTexture3D;

    /*std::unordered_map< std::string, Weak< Buffer > >  m_Buffers;
    std::unordered_map< std::string, Weak< Texture > > m_Textures;
    std::unordered_map< std::string, Arc< Sampler > >  m_Samplers;*/

    SceneResource* m_pSceneResource = nullptr;
};

} // namespace render