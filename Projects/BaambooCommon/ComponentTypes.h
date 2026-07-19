#pragma once
#include "Primitives.h"

enum class eCloudUprezRatio
{
	X1,
	X2,
	X4,
};

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
	None,
	Reinhard,
	ACES,
	Uncharted2,
	Uchimura,
};

enum eDebugDraw
{
	BoundingLine     = 0,
	FrustumWireframe = 1,
};