#pragma once
#include <chrono>
using namespace std::chrono;

class Timer
{
public:
    Timer() : m_ElapsedTime(0.0), m_TotalTime(0.0)
    {
        m_TimePoint0 = std::chrono::high_resolution_clock::now();
	}
    ~Timer() = default;

    void Tick()
    {
        m_TimePoint1 = high_resolution_clock::now();
        duration< double, std::nano > delta = m_TimePoint1 - m_TimePoint0;

        m_TimePoint0  = m_TimePoint1;
        m_ElapsedTime = delta.count();

        m_TotalTime += m_ElapsedTime;
    }

    void Reset()
    {
        m_TimePoint0  = high_resolution_clock::now();
        m_ElapsedTime = 0.0;
        m_TotalTime   = 0.0;
    }

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

