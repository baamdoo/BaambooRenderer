#pragma once
#include "RenderResources.h"

#include <unordered_map>

namespace render 
{

class RenderDevice;

class RenderNode : public ArcBase
{
public:
    RenderNode(RenderDevice& rd, const std::string& name) 
        : m_RenderDevice(rd), m_Name(name), m_bEnabled(true) {}
    virtual ~RenderNode() = default;

    virtual void Apply(CommandContext& context, const SceneRenderView& renderView) = 0;

    virtual void Resize(u32 width, u32 height, u32 depth = 1) { UNUSED(width); UNUSED(height); UNUSED(depth); }

    const std::string& GetName() const { return m_Name; }
    bool IsEnabled() const { return m_bEnabled; }
    void SetEnabled(bool bEnable) { m_bEnabled = bEnable; }

    // Dependency management (for future use) //
    //void AddDependency(RenderNode* node) { m_pDependencies.push_back(node); }
    //
    //void AddOutput(const std::string& name, Arc< Texture > texture) { m_Outputs[name] = texture; }
    //
    //Arc< Texture > GetOutput(const std::string& name) const
    //{
    //    auto it = m_Outputs.find(name);
    //    return (it != m_Outputs.end()) ? it->second : nullptr;
    //}
    //
    //const std::vector< RenderNode* >& GetDependencies() const { return m_pDependencies; }
    //
    //std::vector< RenderNode* >                        m_pDependencies;
    //std::unordered_map< std::string, Arc< Texture > > m_Outputs;
    ////////////////////////////////////////////

protected:
    RenderDevice& m_RenderDevice;

    std::string m_Name;

    bool m_bEnabled;
};

} // namespace render