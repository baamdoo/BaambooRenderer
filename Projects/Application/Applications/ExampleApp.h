#pragma once
#include "BaambooEngine.h"

#include <imgui/imgui.h>

class ExampleApp : public baamboo::Engine
{
using Super = baamboo::Engine;

public:
	virtual void Update(float dt) override;

private:
	virtual bool InitWindow() override;
	virtual bool LoadScene() override;
	virtual void DrawUI() override;
};

