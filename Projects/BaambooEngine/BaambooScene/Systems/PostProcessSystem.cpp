#include "BaambooPch.h"
#include "PostProcessSystem.h"

#include "AtmosphereSystem.h"

namespace baamboo
{

PostProcessSystem::PostProcessSystem(entt::registry& registry)
	: Super(registry)
{
}

void PostProcessSystem::OnComponentConstructed(entt::registry& registry, entt::entity entity)
{
	auto& postProcess = registry.get< PostProcessComponent >(entity);

	postProcess.effectBits = 0;

	postProcess.aa.type        = eAntiAliasingType::TAA;
	postProcess.aa.blendFactor = 0.1f;
	postProcess.aa.sharpness   = 0.5f;

	postProcess.tonemap.op    = eToneMappingOp::Reinhard;
	postProcess.tonemap.gamma = 2.2f;

	Super::OnComponentConstructed(registry, entity);
}

void PostProcessSystem::OnComponentUpdated(entt::registry& registry, entt::entity entity)
{
	Super::OnComponentUpdated(registry, entity);
}

void PostProcessSystem::OnComponentDestroyed(entt::registry& registry, entt::entity entity)
{
	Super::OnComponentDestroyed(registry, entity);
}

std::vector< u64 > PostProcessSystem::UpdateRenderData(const EditorCamera& edCamera)
{
	std::vector< u64 > markedEntities;
	if (m_DirtyEntities.empty())
		return markedEntities;

	m_Registry.view< PostProcessComponent >().each([this, &edCamera, &markedEntities](auto entity, auto& component)
		{
			if (m_bHasData)
				return;

			m_RenderData.id         = entt::to_integral(entity);
			m_RenderData.effectBits = component.effectBits;

			m_RenderData.aa.type        = component.aa.type;
			m_RenderData.aa.blendFactor = component.aa.blendFactor;
			m_RenderData.aa.sharpness   = component.aa.sharpness;

			m_RenderData.tonemap.op    = component.tonemap.op;
			m_RenderData.tonemap.ev100 = component.tonemap.ev100;
			m_RenderData.tonemap.gamma = component.tonemap.gamma;

			m_bHasData = true;
			markedEntities.emplace_back(m_RenderData.id);
		});

	ClearDirtyEntities();
	return markedEntities;
}

void PostProcessSystem::CollectRenderData(SceneRenderView& outView) const
{
	if (m_bHasData)
	{
		outView.postProcess = m_RenderData;
	}
}

void PostProcessSystem::RemoveRenderData(u64 entityId)
{
	if (m_RenderData.id == entityId)
	{
		m_bHasData = false;
	}
}

}
