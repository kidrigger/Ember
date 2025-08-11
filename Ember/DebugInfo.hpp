#pragma once

namespace Ember
{
class DebugInfo
{
  size_t m_VertexCount{ 0 };
  size_t m_DrawCallCount{ 0 };

public:
  static DebugInfo& Instance()
  {
    static DebugInfo instance;
    return instance;
  }

  [[nodiscard]] size_t GetVertexCount() const
  {
    return m_VertexCount;
  }

  [[nodiscard]] size_t GetDrawCallCount() const
  {
    return m_DrawCallCount;
  }

  void PushDrawCall( size_t const vertex_count )
  {
    m_VertexCount += vertex_count;
    m_DrawCallCount++;
  }

  void ClearFrame()
  {
    m_VertexCount   = 0;
    m_DrawCallCount = 0;
  }
};
} // namespace Ember
