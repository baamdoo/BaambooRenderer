#pragma once
#include <chrono>
#include <string>
#include <vector>

namespace render
{

// =========================================================================
// CpuProfileEntry — CPU wall-clock timing for a named scope
// =========================================================================
struct CpuProfileEntry
{
    const char* name;       // caller-owned string (string literal recommended)
    u32         depth;      // 0 = top-level (typically the implicit "Frame" scope)
    double      elapsedMs;  // wall-clock CPU time for this scope
};

// =========================================================================
// CpuProfiler — per-frame, per-thread multi-scope CPU profiler.
// =========================================================================
class CpuProfiler
{
public:
    static CpuProfiler& Thread()
    {
        thread_local CpuProfiler inst;
        return inst;
    }

    void BeginFrame()
    {
        if (!m_Building.empty())
        {
            // Previous frame's data becomes available as "last results".
            m_LastResults = std::move(m_Building);
        }
        m_Building.clear();
        m_StartTimes.clear();
        m_OpenStack.clear();
        m_CurrentDepth = 0;

        BeginMarker("Frame");
    }

    void EndFrame()
    {
        EndMarker(); // close "Frame"
    }

    void BeginMarker(const char* name)
    {
        CpuProfileEntry entry = {
            .name      = name,
            .depth     = m_CurrentDepth,
            .elapsedMs = 0.0,
        };
        const size_t entryIdx = m_Building.size();
        m_Building.push_back(entry);
        m_StartTimes.push_back(std::chrono::steady_clock::now());
        m_OpenStack.push_back(u32(entryIdx));
        ++m_CurrentDepth;
    }

    void EndMarker()
    {
        if (m_OpenStack.empty())
            return; // silent in release; asserts elsewhere catch mismatches

        const u32  entryIdx = m_OpenStack.back();
        m_OpenStack.pop_back();

        const auto end   = std::chrono::steady_clock::now();
        const auto start = m_StartTimes[entryIdx];
        const auto delta = std::chrono::duration_cast< std::chrono::nanoseconds >(end - start).count();
        m_Building[entryIdx].elapsedMs = double(delta) * 1e-6;

        --m_CurrentDepth;
    }

    const std::vector< CpuProfileEntry >& GetLastFrameProfile() const { return m_LastResults; }

private:
    std::vector< CpuProfileEntry >                         m_Building;
    std::vector< CpuProfileEntry >                         m_LastResults;
    std::vector< std::chrono::steady_clock::time_point >   m_StartTimes; // parallel to m_Building
    std::vector< u32 >                                     m_OpenStack;
    u32                                                    m_CurrentDepth = 0;
};

// ================================================================================
// CpuScope — RAII helper for paired BeginMarker / EndMarker on the current thread.
// ================================================================================
class CpuScope
{
public:
    explicit CpuScope(const char* name) { CpuProfiler::Thread().BeginMarker(name); }
    ~CpuScope() { CpuProfiler::Thread().EndMarker(); }
    CpuScope(const CpuScope&) = delete;
    CpuScope& operator=(const CpuScope&) = delete;
};

} // namespace render

#define BAAMBOO_CPU_SCOPE_CONCAT_INNER_(a, b) a##b
#define BAAMBOO_CPU_SCOPE_CONCAT_(a, b) BAAMBOO_CPU_SCOPE_CONCAT_INNER_(a, b)
#define BAAMBOO_CPU_SCOPE(name) ::render::CpuScope BAAMBOO_CPU_SCOPE_CONCAT_(_cpu_scope_, __LINE__)(name)

#define BAAMBOO_PROFILE_SCOPE(ctx, name) \
    BAAMBOO_CPU_SCOPE(name);             \
    BAAMBOO_GPU_SCOPE((ctx), name)

#define BAAMBOO_PROFILE_SCOPE_STATS(ctx, name) \
    BAAMBOO_CPU_SCOPE(name);                   \
    BAAMBOO_GPU_SCOPE_STATS((ctx), name)
