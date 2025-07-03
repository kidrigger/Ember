#pragma once

#include "Runtime.hpp"

namespace Ember
{

class PerfCounter
{
  double static constexpr kSampleCount = 256.0;

  LARGE_INTEGER m_PrevQueryPerfCounter{};
  double        m_FrameTimeMs{ 0.0f };
  double        m_256FrameAvgBuffer[256]{};
  double        m_BufferSumMs{ 0.0f };
  int           m_AvgBufferHead{ 0 };

public:
  void                 Tick();

  [[nodiscard]] double GetAvgFrameTime() const;
  [[nodiscard]] double GetDeltaMilliSeconds() const;
};

} // namespace Ember
