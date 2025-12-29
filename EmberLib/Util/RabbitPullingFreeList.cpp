#include "RabbitPullingFreeList.hpp"

#include "HelperUtils.hpp"

Ember::RabbitPullingFreeList::RabbitPullingFreeList( uint32_t const max_allowed ) : m_MaxAllowed{ max_allowed }
{
  // Sentinel
  ASSERT( max_allowed != UINT32_MAX );
}

uint32_t Ember::RabbitPullingFreeList::Allocate()
{
  if ( not m_Recycled.empty() )
  {
    uint32_t const index = m_Recycled.front();
    m_Recycled.pop_front();
    return index;
  }

  ASSERT( m_MaxReached < m_MaxAllowed );

  uint32_t const index = m_MaxReached++;

  return index;
}

void Ember::RabbitPullingFreeList::Free( uint32_t const index )
{
  ASSERT( std::ranges::find( m_Recycled, index ) == m_Recycled.end() );
  m_Recycled.push_back( index );
}

uint32_t Ember::RabbitPullingFreeList::InUse() const
{
  return m_MaxReached - ( uint32_t )m_Recycled.size();
}
