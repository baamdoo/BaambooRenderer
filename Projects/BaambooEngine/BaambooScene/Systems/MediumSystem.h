#pragma once
#include "SceneSystem.h"

#include <optional>

namespace baamboo
{

class MediumSystem : public SceneSystem< MediumComponent >
{
using Super = SceneSystem< MediumComponent >;
public:
    explicit MediumSystem(entt::registry& registry);

    virtual std::vector< u64 > UpdateRenderData(const float3& cameraPos) override;
    virtual void CollectRenderData(SceneRenderView& outView) const override;
    virtual void RemoveRenderData(u64 entityId) override;

private:
    Arc< const MediumDensityAsset > GetOrLoadDensity(const std::string& filepath);

    std::unordered_map< u64, std::optional< MediumRenderView > > m_RenderData;
    std::unordered_map< std::string, Arc< const MediumDensityAsset > > m_DensityAssets;
};

} // namespace baamboo
