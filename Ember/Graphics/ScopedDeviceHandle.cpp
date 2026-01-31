#include "ScopedDeviceHandle.hpp"

#include "RenderDevice.hpp"

Ember::ScopedHandlePair::ScopedHandlePair( BindlessManager* bindless, SRVHandle const srv, UAVHandle const uav )
  : m_Bindless{ bindless }, m_SRV{ srv }, m_UAV{ uav }
{}

Ember::SRVHandle Ember::ScopedHandlePair::GetSRV() const noexcept
{
  return m_SRV;
}

Ember::UAVHandle Ember::ScopedHandlePair::GetUAV() const noexcept
{
  return m_UAV;
}

Ember::ScopedHandlePair::~ScopedHandlePair()
{
  if ( not m_Bindless ) return;
  m_Bindless->Free( m_SRV );
  m_Bindless->Free( m_UAV );
}

Ember::ScopedHandlePair::ScopedHandlePair( ScopedHandlePair&& other ) noexcept
  : m_Bindless{ other.m_Bindless }, m_SRV{ other.m_SRV }, m_UAV{ other.m_UAV }
{
  other.m_Bindless = nullptr;
  other.m_SRV      = {};
  other.m_UAV      = {};
}

Ember::ScopedHandlePair& Ember::ScopedHandlePair::operator=( ScopedHandlePair&& other ) noexcept
{
  if ( this == &other ) return *this;

  std::swap( m_Bindless, other.m_Bindless );
  std::swap( m_SRV, other.m_SRV );
  std::swap( m_UAV, other.m_UAV );

  return *this;
}
