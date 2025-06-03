#pragma once
#define BAAMBOO_ENGINE
#include "BaambooCore/Common.h"

enum eComponentType
{
    CTransform = 0,
    CStaticMesh = 1,
    CDynamicMesh = 2,
    CMaterial = 3,
    CPointLight = 4,

    // ...
    NumComponents
};
