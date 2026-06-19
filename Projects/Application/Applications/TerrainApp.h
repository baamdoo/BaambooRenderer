#pragma once
#include "BaambooEngine.h"
#include "BaambooScene/Entity.h"
#include "BaambooScene/VoxelTerrain/ProceduralTerrain.h"
#include "BaambooScene/VoxelTerrain/VoxelTerrainDebug.h"

class TerrainApp : public baamboo::Engine
{
using Super = baamboo::Engine;
public:
	virtual void Initialize(eRendererAPI api) override;
	virtual void Update(float dt) override;
	virtual void Release() override;

private:
	virtual bool InitWindow() override;
	virtual bool LoadScene() override;
	virtual void DrawUI() override;

	void ConfigureRenderGraph();
	void ConfigureSceneObjects();
	void RebuildVoxelTerrain(bool bSceneAlreadyLocked = false);
	void RefreshVoxelTerrainMeshComponent();
	void RefreshVoxelTerrainDebugLines(bool bSceneAlreadyLocked = false);

	baamboo::CameraController_FirstPerson m_CameraController;

	baamboo::ProceduralTerrain      m_VoxelTerrain;
	baamboo::VoxelTerrainSettings   m_VoxelTerrainSettings = {};
	baamboo::VoxelTerrainDebugStats m_VoxelTerrainStats    = {};
	u32                             m_VoxelChunkIndex      = kInvalidIndex;
	baamboo::Entity                 m_VoxelTerrainEntity;

	bool  m_bVoxelMeshVisible        = true;
	bool  m_bVoxelWireframeVisible   = false;
	bool  m_bVoxelChunkBoundsVisible = true;
	bool  m_bVoxelNormalsVisible     = false;
	float m_VoxelNormalLineLength    = 1.5f;
	i32   m_VoxelNormalStride        = 12;
	i32   m_VoxelNormalMaxCount      = 512;
};
