#pragma once

#pragma warning( disable: 4238 )

#define NOGDI
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <shellapi.h> // For CommandLineToArgvW

// In order to define a function called CreateWindow, the Windows macro needs to be undefined.
#if defined(CreateWindow)
#undef CreateWindow
#endif

#pragma warning( disable: 4238 )
#pragma warning( disable: 4099 )


//-------------------------------------------------------------------------
// STL
//-------------------------------------------------------------------------
#include <algorithm>
#include <type_traits>
#include <iostream>
#include <queue>
#include <deque>
#include <map>
#include <mutex>
#include <vector>


//-------------------------------------------------------------------------
// Includes
//-------------------------------------------------------------------------
#include "Defines.h"
#include "Pointer.hpp"
#include "ShaderTypes.h"
#include "Core/Dx12Core.h"