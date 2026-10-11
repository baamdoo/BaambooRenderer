#include "BaambooPch.h"
#include "MediumSystem.h"

#include <iostream>
#include <nanovdb/util/GridValidator.h>
#include <nanovdb/util/IO.h>

#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <type_traits>

static_assert(NANOVDB_MAJOR_VERSION_NUMBER == 32 && NANOVDB_MINOR_VERSION_NUMBER == 3 &&
    NANOVDB_PATCH_VERSION_NUMBER == 3, "Medium density assets require NanoVDB 32.3.3");

namespace baamboo
{

namespace
{

// The readGrid/grid<float>/exception pattern follows the pinned NanoVDB example:
//     https://github.com/AcademySoftwareFoundation/openvdb/blob/414bed84c2fc22e188eac7b611aa85c7edd7a5a9/nanovdb/nanovdb/examples/ex_read_nanovdb_sphere/read_nanovdb_sphere.cc
Arc< const MediumDensityAsset > LoadMediumDensity(const std::string& path)
{
    const auto metadata = nanovdb::io::readGridMetaData(path);
    if (metadata.size() != 1 || metadata[0].gridName != "density" ||
        metadata[0].codec != nanovdb::io::Codec::NONE ||
        metadata[0].gridType != nanovdb::GridType::Float ||
        metadata[0].gridClass != nanovdb::GridClass::FogVolume)
        throw std::runtime_error("Expected one uncompressed Float32 density FogVolume: " + path);

    const auto handle = nanovdb::io::readGrid(path, std::string("density"));
    const auto* pGrid = handle.grid<float>();
    if (!pGrid || pGrid->version().getMajor() != 32 || pGrid->version().getMinor() != 3 ||
        pGrid->version().getPatch() != 3 || pGrid->gridCount() != 1 ||
        pGrid->gridIndex() != 0 || pGrid->gridClass() != nanovdb::GridClass::FogVolume ||
        pGrid->gridSize() != metadata[0].gridSize ||
        pGrid->gridSize() >= std::numeric_limits<u32>::max() || pGrid->gridSize() % 32u != 0u)
        throw std::runtime_error("Unsupported density payload: " + path);

    const auto error = nanovdb::GridValidator<float>::check(*pGrid, true);
    if (!error.empty())
        throw std::runtime_error("Invalid density grid: " + error);

    const auto& treeRoot = pGrid->tree().root();
    if (treeRoot.background() != 0.0f || !pGrid->hasBBox() || !pGrid->hasMinMax() ||
        !std::isfinite(treeRoot.minimum()) || !std::isfinite(treeRoot.maximum()) ||
        treeRoot.minimum() < 0.0f || treeRoot.maximum() < treeRoot.minimum())
        throw std::runtime_error("Density must have zero background and finite nonnegative bounds");

    // Reference: https://github.com/AcademySoftwareFoundation/openvdb/blob/414bed84c2fc22e188eac7b611aa85c7edd7a5a9/nanovdb/nanovdb/util/GridStats.h
    float maxDensity = 0.0f;
    const auto includeValue = [&maxDensity](float value)
    {
        if (!std::isfinite(value) || value < 0.0f)
            throw std::runtime_error("Density contains a nonfinite or negative stored value");
        maxDensity = std::max(maxDensity, value);
    };
    const auto scanNode = [&includeValue](const auto& self, const auto& node) -> void
    {
        using Node = std::remove_cvref_t<decltype(node)>;
        for (u32 i = 0; i < Node::SIZE; ++i)
        {
            if constexpr (Node::LEVEL == 0)
                includeValue(node.getValue(i));
            else if (node.childMask().isOn(i))
                self(self, *node.data()->getChild(i));
            else
                includeValue(node.data()->mTable[i].value);
        }
    };
    includeValue(treeRoot.background());
    for (u32 i = 0; i < treeRoot.tileCount(); ++i)
    {
        const auto* pTile = treeRoot.data()->tile(i);
        if (pTile->isChild())
            scanNode(scanNode, *treeRoot.data()->getChild(pTile));
        else
            includeValue(pTile->value);
    }

    auto pAsset = MakeArc< MediumDensityAsset >();
    pAsset->maxDensity = maxDensity;
    pAsset->words.resize(static_cast<size_t>(pGrid->gridSize() / sizeof(u32)));
    std::memcpy(pAsset->words.data(), pGrid, static_cast<size_t>(pGrid->gridSize()));
    
    const auto origin = pGrid->worldToIndex(nanovdb::Vec3d(0.0));
    for (u32 column = 0; column < 3; ++column)
    {
        nanovdb::Vec3d axis(0.0);
        axis[column] = 1.0;
        const auto mapped = pGrid->worldToIndex(axis) - origin;
        pAsset->mLocalToIndex[column] = float4(
            static_cast<float>(mapped[0]), static_cast<float>(mapped[1]),
            static_cast<float>(mapped[2]), 0.0f);
    }
    pAsset->mLocalToIndex[3] = float4(
        static_cast<float>(origin[0]), static_cast<float>(origin[1]),
        static_cast<float>(origin[2]), 1.0f);
    return pAsset;
}

} // namespace

MediumSystem::MediumSystem(entt::registry& registry)
    : Super(registry)
{
}

// Reference: https://www.openvdb.org/documentation/doxygen/NanoVDB_HelloWorld.html
Arc< const MediumDensityAsset > MediumSystem::GetOrLoadDensity(const std::string& filepath)
{
    try
    {
        const std::string path = fs::absolute(filepath).lexically_normal().generic_string();
        auto assetIt = m_DensityAssets.find(path);
        if (assetIt != m_DensityAssets.end())
            return assetIt->second;

        auto pAsset = LoadMediumDensity(path);
        m_DensityAssets.emplace(path, pAsset);
        return pAsset;
    }
    catch (const std::exception& error)
    {
        fprintf(stderr, "Medium Loading Error (%s) - %s\n", filepath.c_str(), error.what());
        return nullptr;
    }
}

std::vector< u64 > MediumSystem::UpdateRenderData(const float3& cameraPos)
{
    UNUSED(cameraPos);

    for (auto entity : m_ExpiredEntities)
        RemoveRenderData(static_cast<u64>(entt::to_integral(entity)));
    m_ExpiredEntities.clear();

    std::vector< u64 > markedEntities;
    for (auto entity : m_DirtyEntities)
    {
        if (!m_Registry.valid(entity) || !m_Registry.all_of< MediumComponent >(entity))
            continue;

        const u64 id = static_cast<u64>(entt::to_integral(entity));
        const auto& component = m_Registry.get< MediumComponent >(entity);

        MediumRenderView view = {};
        view.data.sigmaA       = component.sigmaA;
        view.data.sigmaS       = component.sigmaS;
        view.data.phaseG       = component.phaseG;
        view.data.densityScale = component.densityScale;
        view.data.mediumFlags  = component.bAirContributor ? MEDIUM_FLAG_AIR_CONTRIBUTOR : 0u;
        if (!component.densityFile.empty())
        {
            view.pDensity = GetOrLoadDensity(component.densityFile);
            if (!view.pDensity)
            {
                m_RenderData[id] = std::nullopt;
                markedEntities.emplace_back(id);
                continue;
            }
        }

        m_RenderData[id] = std::move(view);
        markedEntities.emplace_back(id);
    }

    ClearDirtyEntities();
    return markedEntities;
}

void MediumSystem::CollectRenderData(SceneRenderView& outView) const
{
    for (const auto& [entity, medium] : m_RenderData)
    {
        // Mesh and transform collection run first. Do not create phantom draws.
        auto draw = outView.draws.find(static_cast<u32>(entity));
        if (draw == outView.draws.end() || !IsValidIndex(draw->second.mesh))
            continue;

        if (!medium)
        {
            // A requested grid failed to load; do not render a homogeneous substitute.
            outView.draws.erase(draw);
            continue;
        }

        draw->second.medium = static_cast<u32>(outView.media.size());
        outView.media.push_back(*medium);
    }
}

void MediumSystem::RemoveRenderData(u64 entityId)
{
    m_RenderData.erase(entityId);
}

} // namespace baamboo
