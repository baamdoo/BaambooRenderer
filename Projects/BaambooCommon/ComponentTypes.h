#pragma once

enum ePostProcess
{
	AntiAliasing = 0,
	Bloom = 1,
	HeightFog = 2,
};

enum class eAntiAliasingType
{
	TAA,
	FXAA,
};

enum class eToneMappingOp
{
	Reinhard,
	ACES,
	Uncharted2,
};