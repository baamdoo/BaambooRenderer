#include "Pointer.hpp"

#include <mutex>
#include <unordered_set>

static std::unordered_set< void* > s_LiveReferences;
static std::mutex                  s_LiveReferenceMutex;

namespace ptr_util
{

bool IsLive(void* ptr)
{
	return s_LiveReferences.find(ptr) != s_LiveReferences.end();
}

void AddToLiveReferences(void* ptr)
{
	assert(ptr);
	std::scoped_lock< std::mutex > lock(s_LiveReferenceMutex);
	s_LiveReferences.insert(ptr);
}

void RemoveFromLiveReferences(void* ptr)
{
	assert(ptr);
	assert(s_LiveReferences.find(ptr) != s_LiveReferences.end());
	std::scoped_lock< std::mutex > lock(s_LiveReferenceMutex);
	s_LiveReferences.erase(ptr);
}

} // namespace ptr_util 
