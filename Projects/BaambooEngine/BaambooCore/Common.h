#pragma once

//-------------------------------------------------------------------------
// STL
//-------------------------------------------------------------------------
#include <cassert>
#include <chrono>
#include <unordered_map>
#include <functional>
#include <thread>
#include <vector>
#include <mutex>
#include <string>

//-------------------------------------------------------------------------
// Includes
//-------------------------------------------------------------------------
#include "Defines.h"
#include "Primitives.h"
#include "MathTypes.h"
#include "Pointer.hpp"

enum class eWorldDistanceScaleType
{
	CM,
	M,
	KM
};