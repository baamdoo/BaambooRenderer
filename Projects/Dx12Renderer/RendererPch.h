#pragma once

#pragma warning( disable: 4238 )

#define NOMINMAX
#include <Windows.h>
#include <shellapi.h> // For CommandLineToArgvW

// In order to define a function called CreateWindow, the Windows macro needs to be undefined.
#if defined(CreateWindow)
#undef CreateWindow
#endif

#pragma warning( disable: 4238 )
#pragma warning( disable: 4099 )

#include <algorithm>
#include <type_traits>
#include <iostream>
#include <queue>
#include <deque>
#include <map>

#include "BaambooCore/Common.h"
#include "Core/Dx12Core.h"