#pragma once

#include "Runtime.hpp"

namespace Ember
{

class ScopedHandle
{
  HANDLE m_Handle{ INVALID_HANDLE_VALUE };

public:
  ScopedHandle() = default;
  ScopedHandle( HANDLE handle );

  ScopedHandle( ScopedHandle const& other ) = delete;
  ScopedHandle( ScopedHandle&& other ) noexcept;
  ScopedHandle& operator=( ScopedHandle const& other ) = delete;
  ScopedHandle& operator=( ScopedHandle&& other ) noexcept;

  operator HANDLE() const;

  ~ScopedHandle();
};

} // namespace Ember
