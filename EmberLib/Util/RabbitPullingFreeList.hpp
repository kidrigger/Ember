#pragma once

#include <deque>

namespace Ember
{

// Freelist that returns indexes.
// Prefers recycled indexes, else pulls a new rabbit (index) out of a hat.
class RabbitPullingFreeList
{
  std::deque<uint32_t> m_Recycled{};
  uint32_t             m_MaxReached{ 0 };
  uint32_t             m_MaxAllowed{ 0 };

public:
  RabbitPullingFreeList() = default;
  explicit RabbitPullingFreeList( uint32_t max_allowed );

  uint32_t               Allocate();
  void                   Free( uint32_t index );

  [[nodiscard]] uint32_t InUse() const;
};

} // namespace Ember
