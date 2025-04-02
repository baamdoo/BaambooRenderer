#pragma once
#include <chrono>

class Timer
{
public:
    Timer();
    ~Timer() = default;

    void Tick();

    void Reset();

    inline double GetDeltaSeconds() const { return m_ElapsedTime * 1e-9; }
    inline double GetDeltaMilliseconds() const { return m_ElapsedTime * 1e-6; }
    inline double GetDeltaMicroseconds() const { return m_ElapsedTime * 1e-3; }
    inline double GetDeltaNanoseconds() const { return m_ElapsedTime; }

    inline double GetTotalSeconds() const { return m_TotalTime * 1e-9; }
    inline double GetTotalMilliseconds() const { return m_TotalTime * 1e-6; }
    inline double GetTotalMicroseconds() const { return m_TotalTime * 1e-3; }
    inline double GetTotalNanoseconds() const { return m_TotalTime; }

private:
    std::chrono::high_resolution_clock::time_point m_TimePoint0, m_TimePoint1;

    double m_ElapsedTime;
    double m_TotalTime;
};

