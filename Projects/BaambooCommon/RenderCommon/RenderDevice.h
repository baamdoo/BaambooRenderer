#pragma once
#include "RenderResources.h"

namespace render 
{

class RenderDevice
{
public:
    virtual ~RenderDevice() = default;

    virtual void Flush() = 0;

    // Resource creation
    virtual Arc< Buffer > CreateBuffer(const std::string& name, Buffer::CreationInfo&& desc) = 0;
    virtual Arc< Texture > CreateTexture(const std::string& name, Texture::CreationInfo&& desc) = 0;
    virtual Arc< Texture > CreateEmptyTexture(const std::string& name = "") = 0;

    virtual Arc< RenderTarget > CreateEmptyRenderTarget(const std::string& name = "") = 0;

    virtual Arc< Sampler > CreateSampler(const std::string& name, Sampler::CreationInfo&& info) = 0;

    virtual Arc< Shader > CreateShader(const std::string& name, Shader::CreationInfo&& info) = 0;

    virtual Box< GraphicsPipeline > CreateGraphicsPipeline(const std::string& name) = 0;
    virtual Box< ComputePipeline > CreateComputePipeline(const std::string& name) = 0;

    virtual Box< render::SceneResource > CreateSceneResource() = 0;

    virtual ResourceManager& GetResourceManager() const = 0;

    inline u32 ContextIndex() const { return m_ContextIndex; }
    inline u32 NumContexts() const { return m_NumContexts; }
    void SetNumContexts(u32 num) { m_NumContexts = num; }

    u32 WindowWidth() const { return m_WindowWidth; }
    u32 WindowHeight() const { return m_WindowHeight; }
    void SetWindowWidth(u32 width) { m_WindowWidth = width; }
    void SetWindowHeight(u32 height) { m_WindowHeight = height; }

protected:
    u32 m_NumContexts  = 0;
    u32 m_ContextIndex = 0;

    u32 m_WindowWidth  = 0;
    u32 m_WindowHeight = 0;
};

} // namespace render