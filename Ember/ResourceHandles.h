#pragma once

#include <cstdint>

namespace Ember
{

class Buffer
{
  uint32_t m_Index;

  friend class BufferManager;

  explicit Buffer( uint32_t const id ) : m_Index{ id }
  {}
};

} // namespace Ember
