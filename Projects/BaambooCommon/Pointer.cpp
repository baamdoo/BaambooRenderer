#include "Pointer.hpp"

#include <mutex>
#include <unordered_set>

static std::unordered_set< void* > s_LiveReferences;
static std::mutex                  s_LiveReferenceMutex;

namespace ptr_util
{

BAAMBOO_API bool IsLive(void* ptr)
{
	std::scoped_lock< std::mutex > lock(s_LiveReferenceMutex);
	return s_LiveReferences.find(ptr) != s_LiveReferences.end();
}

BAAMBOO_API void AddToLiveReferences(void* ptr)
{
	assert(ptr);
	std::scoped_lock< std::mutex > lock(s_LiveReferenceMutex);
	s_LiveReferences.insert(ptr);
}

BAAMBOO_API void RemoveFromLiveReferences(void* ptr)
{
	assert(ptr);
	assert(s_LiveReferences.find(ptr) != s_LiveReferences.end());
	std::scoped_lock< std::mutex > lock(s_LiveReferenceMutex);
	s_LiveReferences.erase(ptr);
}

} // namespace ptr_util 
