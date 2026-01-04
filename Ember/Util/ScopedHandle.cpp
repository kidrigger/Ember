#include "ScopedHandle.hpp"

#include <utility>

Ember::ScopedHandle::ScopedHandle( HANDLE const handle ) : m_Handle{ handle }
{}

Ember::ScopedHandle::ScopedHandle( ScopedHandle&& other ) noexcept : m_Handle{ other.m_Handle }
{
  other.m_Handle = INVALID_HANDLE_VALUE;
}

Ember::ScopedHandle& Ember::ScopedHandle::operator=( ScopedHandle&& other ) noexcept
{
  if ( this == &other ) return *this;
  std::swap( m_Handle, other.m_Handle );
  return *this;
}

Ember::ScopedHandle::operator HANDLE() const
{
  return m_Handle;
}

Ember::ScopedHandle::~ScopedHandle()
{
  ::CloseHandle( m_Handle );
}
