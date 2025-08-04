#include "ResourceTracker.hpp"

#include "RenderDevice.hpp"
#include "Util/HelperUtils.hpp"

void Ember::ResourceTracker::HandleVariantDeleter::operator()( CBVHandle const handle ) const
{
  Device->FreeHandle( handle );
}

void Ember::ResourceTracker::HandleVariantDeleter::operator()( SRVHandle const handle ) const
{
  Device->FreeHandle( handle );
}

void Ember::ResourceTracker::HandleVariantDeleter::operator()( UAVHandle const handle ) const
{
  Device->FreeHandle( handle );
}

void Ember::ResourceTracker::HandleVariantDeleter::operator()( SamplerHandle const handle ) const
{
  Device->FreeHandle( handle );
}

Ember::ResourceTracker::ResourceTracker( RenderDevice* device, std::pmr::polymorphic_allocator<> const& allocator )
  : m_Device{ device }, m_Resources{ allocator }, m_Handles{ allocator }, m_Barriers{ allocator }
{}

void Ember::ResourceTracker::PushResource( ComPtr<IUnknown> resource )
{
  m_Resources.push_back( std::move( resource ) );
}

void Ember::ResourceTracker::PushHandle( CBVHandle const handle )
{
  m_Handles.push_back( handle );
}

void Ember::ResourceTracker::PushHandle( SRVHandle const handle )
{
  m_Handles.push_back( handle );
}

void Ember::ResourceTracker::PushHandle( UAVHandle const handle )
{
  m_Handles.push_back( handle );
}

void Ember::ResourceTracker::PushHandle( SamplerHandle const handle )
{
  m_Handles.push_back( handle );
}

void Ember::ResourceTracker::PushBarrier( CD3DX12_RESOURCE_BARRIER const& barrier )
{
  m_Barriers.push_back( barrier );
}

void Ember::ResourceTracker::Clear( std::vector<D3D12_RESOURCE_BARRIER>* pending_barriers )
{
  ASSERT_M( m_Barriers.empty() or pending_barriers, "If barriers exist, they must be inserted." );

  for ( auto& one_handle : m_Handles )
  {
    std::visit( HandleVariantDeleter{ m_Device }, one_handle );
  }
  if ( pending_barriers )
  {
    pending_barriers->insert( pending_barriers->end(), m_Barriers.begin(), m_Barriers.end() );
  }

  m_Handles.clear();
  m_Resources.clear();
  m_Barriers.clear();
}

bool Ember::ResourceTracker::IsEmpty() const
{
  return m_Resources.empty() and m_Barriers.empty() and m_Handles.empty();
}
