#include "ResourceTracker.hpp"

#include <Graphics/RenderDevice.hpp>
#include <Util/HelperUtils.hpp>

Ember::ResourceTracker::ResourceTracker( RenderDevice* device, std::pmr::polymorphic_allocator<> const& allocator )
  : m_Device{ device }
  , m_Resources{ allocator }
  , m_CBVHandles{ allocator }
  , m_SRVHandles{ allocator }
  , m_UAVHandles{ allocator }
  , m_SamplerHandles{ allocator }
  , m_Barriers{ allocator }
{}

void Ember::ResourceTracker::PushResource( ComPtr<IUnknown> resource )
{
  m_Resources.push_back( std::move( resource ) );
}

void Ember::ResourceTracker::PushHandle( CBVHandle const handle )
{
  m_CBVHandles.push_back( handle );
}

void Ember::ResourceTracker::PushHandle( SRVHandle const handle )
{
  m_SRVHandles.push_back( handle );
}

void Ember::ResourceTracker::PushHandle( UAVHandle const handle )
{
  m_UAVHandles.push_back( handle );
}

void Ember::ResourceTracker::PushHandle( SamplerHandle const handle )
{
  m_SamplerHandles.push_back( handle );
}

void Ember::ResourceTracker::PushBarrier( CD3DX12_RESOURCE_BARRIER const& barrier )
{
  m_Barriers.push_back( barrier );
}

void Ember::ResourceTracker::Clear( std::vector<D3D12_RESOURCE_BARRIER>* pending_barriers )
{
  ASSERT_M( m_Barriers.empty() or pending_barriers, "If barriers exist, they must be inserted." );

  for ( auto& one_handle : m_CBVHandles )
  {
    m_Device->FreeHandle( one_handle );
  }
  for ( auto& one_handle : m_SRVHandles )
  {
    m_Device->FreeHandle( one_handle );
  }
  for ( auto& one_handle : m_UAVHandles )
  {
    m_Device->FreeHandle( one_handle );
  }
  for ( auto& one_handle : m_SamplerHandles )
  {
    m_Device->FreeHandle( one_handle );
  }
  if ( pending_barriers )
  {
    pending_barriers->insert( pending_barriers->end(), m_Barriers.begin(), m_Barriers.end() );
  }

  m_CBVHandles.clear();
  m_SRVHandles.clear();
  m_UAVHandles.clear();
  m_SamplerHandles.clear();
  m_Resources.clear();
  m_Barriers.clear();
}

bool Ember::ResourceTracker::IsEmpty() const
{
  return m_Resources.empty() and m_Barriers.empty() and m_CBVHandles.empty() and m_SRVHandles.empty() and
         m_UAVHandles.empty() and m_SamplerHandles.empty();
}
