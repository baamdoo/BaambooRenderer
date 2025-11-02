#pragma once
#include "BaambooEngine.h"

class BistroApp : public baamboo::Engine
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

	baamboo::CameraController_FirstPerson m_CameraController;
};

