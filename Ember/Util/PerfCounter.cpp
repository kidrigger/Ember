#include "PerfCounter.hpp"

void Ember::PerfCounter::Tick()
{
  LARGE_INTEGER perf_counter, freq;
  ::QueryPerformanceCounter( &perf_counter ); // Always returns true on Win XP or later.
  ::QueryPerformanceFrequency( &freq );       // Always returns true on Win XP or later.

  m_FrameTimeMs  = 1000.0f * ( perf_counter.QuadPart - m_PrevQueryPerfCounter.QuadPart ) / ( double )freq.QuadPart;

  m_BufferSumMs -= m_256FrameAvgBuffer[m_AvgBufferHead];
  m_BufferSumMs += m_FrameTimeMs;
  m_256FrameAvgBuffer[m_AvgBufferHead++]  = m_FrameTimeMs;
  m_AvgBufferHead                        %= 256;

  m_PrevQueryPerfCounter                  = perf_counter;
}

double Ember::PerfCounter::GetAvgFrameTime() const
{
  return m_BufferSumMs / kSampleCount;
}

double Ember::PerfCounter::GetDeltaMilliSeconds() const
{
  return m_BufferSumMs;
}
