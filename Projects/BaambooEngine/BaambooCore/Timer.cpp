#include "BaambooPch.h"
#include "Timer.h"

using namespace std::chrono;

Timer::Timer() 
{
    m_TimePoint0 = high_resolution_clock::now();
}

void Timer::Tick()
{
    m_TimePoint1 = high_resolution_clock::now();
    duration< double, std::nano > delta = m_TimePoint1 - m_TimePoint0;

    m_TimePoint0 = m_TimePoint1;
    m_ElapsedTime = delta.count();

    m_TotalTime += m_ElapsedTime;
}

void Timer::Reset()
{
    m_TimePoint0 = high_resolution_clock::now();
    m_ElapsedTime = 0.0;
    m_TotalTime = 0.0;
}