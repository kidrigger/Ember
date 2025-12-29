#pragma once

#include <concepts>
#include <type_traits>
#include "BindlessManager.hpp"
#include "DeviceHandle.hpp"

namespace Ember
{

template <std::derived_from<DeviceHandle> T>
class Scoped
{
  using Handle = T;
  BindlessManager* m_Bindless{ nullptr };
  Handle           m_Handle;

public:
  Scoped() = default;
  Scoped( BindlessManager* bindless, Handle handle ) : m_Bindless{ bindless }, m_Handle{ handle }
  {}

  operator Handle() const
  {
    return m_Handle;
  }

  ~Scoped()
  {
    if ( not m_Bindless ) return;
    m_Bindless->Free( m_Handle );
  }

  Scoped( Scoped const& other )            = delete;
  Scoped& operator=( Scoped const& other ) = delete;
  Scoped( Scoped&& other ) noexcept : m_Bindless{ other.m_Bindless }, m_Handle{ other.m_Handle }
  {
    other.m_Bindless = nullptr;
    other.m_Handle   = {};
  }

  Scoped& operator=( Scoped&& other ) noexcept
  {
    if ( this == &other ) return *this;

    std::swap( m_Bindless, other.m_Bindless );
    std::swap( m_Handle, other.m_Handle );

    return *this;
  }
};
} // namespace Ember
