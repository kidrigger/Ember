#pragma once

#include "Runtime.hpp"

namespace Ember
{

class PerfCounter
{
  double constexpr static kSampleCount = 256.0;
  double constexpr static kMaxDeltaMs  = 1000.0 / 24.0; // Below 24fps, I'd rather slow down than jump.

  LARGE_INTEGER m_PrevQueryPerfCounter{};
  double        m_FrameTimeMs{ 0.0f };
  double        m_256FrameAvgBuffer[256]{};
  double        m_BufferSumMs{ 0.0f };
  int           m_AvgBufferHead{ 0 };
  double        m_SampleCount{ 0.0f };

public:
  PerfCounter();

  void                 Tick();

  [[nodiscard]] double GetAvgFrameTime() const;
  [[nodiscard]] double GetDeltaMilliSeconds() const;
};

} // namespace Ember
