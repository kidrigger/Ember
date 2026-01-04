#include "LightHandle.hpp"

Ember::LightHandle::LightHandle( uint16_t const inner, uint16_t const generation )
  : m_Inner{ inner }, m_Generation{ generation }
{}

uint16_t Ember::LightHandle::GetIndex() const
{
  return m_Inner;
}

uint16_t Ember::LightHandle::GetGeneration() const
{
  return m_Generation;
}

Ember::OmniLightHandle::OmniLightHandle( uint16_t const inner, uint16_t const generation )
  : LightHandle{ inner, generation }
{}

Ember::DirLightHandle::DirLightHandle( uint16_t const inner, uint16_t const generation )
  : LightHandle{ inner, generation }
{}

Ember::SpotLightHandle::SpotLightHandle( uint16_t const inner, uint16_t const generation )
  : LightHandle{ inner, generation }
{}
