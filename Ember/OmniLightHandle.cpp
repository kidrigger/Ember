#include "OmniLightHandle.hpp"

Ember::OmniLightHandle::OmniLightHandle( uint16_t const inner, uint16_t const generation )
  : m_Inner{ inner }, m_Generation{ generation }
{}

uint16_t Ember::OmniLightHandle::GetIndex() const
{
  return m_Inner;
}

uint16_t Ember::OmniLightHandle::GetGeneration() const
{
  return m_Generation;
}

std::strong_ordering Ember::OmniLightHandle::operator<=>( OmniLightHandle const& other ) const
{
  std::strong_ordering const x = m_Generation <=> other.m_Generation;
  if ( x == 0 )
  {
    return m_Inner <=> other.m_Inner;
  }
  return x;
}
