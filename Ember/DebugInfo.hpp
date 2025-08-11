#pragma once

namespace Ember
{
class DebugInfo
{
  size_t m_VertexCount{ 0 };

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

  void PushVertexCount( size_t const value )
  {
    m_VertexCount += value;
  }

  void ClearFrame()
  {
    m_VertexCount = 0;
  }
};
} // namespace Ember
