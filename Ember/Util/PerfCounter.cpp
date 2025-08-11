#include "PerfCounter.hpp"

#include <algorithm>

Ember::PerfCounter::PerfCounter()
{
  ZeroMemory( m_256FrameAvgBuffer, 256 * sizeof( m_256FrameAvgBuffer[0] ) );
  ::QueryPerformanceCounter( &m_PrevQueryPerfCounter );
}

void Ember::PerfCounter::Tick()
{
  LARGE_INTEGER perf_counter, freq;
  ::QueryPerformanceCounter( &perf_counter ); // Always returns true on Win XP or later.
  ::QueryPerformanceFrequency( &freq );       // Always returns true on Win XP or later.

  m_FrameTimeMs  = ( 1000.0 * ( perf_counter.QuadPart - m_PrevQueryPerfCounter.QuadPart ) ) / ( double )freq.QuadPart;

  m_BufferSumMs -= m_256FrameAvgBuffer[m_AvgBufferHead];
  m_BufferSumMs += m_FrameTimeMs;
  m_256FrameAvgBuffer[m_AvgBufferHead++]  = m_FrameTimeMs;
  m_AvgBufferHead                        %= 256;

  m_SampleCount                           = std::min( m_SampleCount + 1.0f, kSampleCount );

  m_PrevQueryPerfCounter                  = perf_counter;
}

double Ember::PerfCounter::GetAvgFrameTime() const
{
  return m_BufferSumMs / m_SampleCount;
}

double Ember::PerfCounter::GetDeltaMilliSeconds() const
{
  return std::clamp( m_FrameTimeMs, 0.0, kMaxDeltaMs );
}
