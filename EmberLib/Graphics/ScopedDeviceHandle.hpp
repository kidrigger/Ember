#pragma once

#include <concepts>
#include <type_traits>
#include "BindlessManager.hpp"
#include "DeviceHandle.hpp"

namespace Ember
{

class ScopedHandlePair
{
  BindlessManager* m_Bindless{ nullptr };
  SRVHandle        m_SRV;
  UAVHandle        m_UAV;

public:
  ScopedHandlePair() = default;
  ScopedHandlePair( BindlessManager* bindless, SRVHandle srv, UAVHandle uav = {} )
    : m_Bindless{ bindless }, m_SRV{ srv }, m_UAV{ uav }
  {}

  SRVHandle GetSRV() const noexcept
  {
    return m_SRV;
  }

  UAVHandle GetUAV() const noexcept
  {
    return m_UAV;
  }

  ~ScopedHandlePair()
  {
    if ( not m_Bindless ) return;
    m_Bindless->Free( m_SRV );
    m_Bindless->Free( m_UAV );
  }

  ScopedHandlePair( ScopedHandlePair const& other )            = delete;
  ScopedHandlePair& operator=( ScopedHandlePair const& other ) = delete;
  ScopedHandlePair( ScopedHandlePair&& other ) noexcept
    : m_Bindless{ other.m_Bindless }, m_SRV{ other.m_SRV }, m_UAV{ other.m_UAV }
  {
    other.m_Bindless = nullptr;
    other.m_SRV      = {};
    other.m_UAV      = {};
  }

  ScopedHandlePair& operator=( ScopedHandlePair&& other ) noexcept
  {
    if ( this == &other ) return *this;

    std::swap( m_Bindless, other.m_Bindless );
    std::swap( m_SRV, other.m_SRV );
    std::swap( m_UAV, other.m_UAV );

    return *this;
  }
};


} // namespace Ember
