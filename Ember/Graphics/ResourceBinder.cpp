#include "ResourceBinder.hpp"

#include <algorithm>
#include "RenderDevice.hpp"

Ember::ResourceBinder::ResourceBinder( BindlessManager* bindless, std::pmr::polymorphic_allocator<> const& allocator )
  : m_Bindless{ bindless }, m_UsedResources{ allocator }
{}

Ember::CBVHandle Ember::ResourceBinder::BindCBV( Buffer const& buffer ) noexcept
{
  auto handle = buffer.GetCBVHandle();
  Track( buffer.GetBuffer() );

  return handle;
}

Ember::SRVHandle Ember::ResourceBinder::BindSRV( Buffer const& buffer ) noexcept
{
  auto handle = buffer.GetSRVHandle();
  Track( buffer.GetBuffer() );

  return handle;
}

Ember::UAVHandle Ember::ResourceBinder::BindUAV( Buffer const& buffer ) noexcept
{
  auto handle = buffer.GetUAVHandle();
  Track( buffer.GetBuffer() );

  return handle;
}

Ember::SRVHandle Ember::ResourceBinder::BindSRV( Texture const& texture ) noexcept
{
  auto handle = texture.GetSRVHandle();
  Track( texture.GetTexture() );

  return handle;
}

Ember::UAVHandle Ember::ResourceBinder::BindUAV( Texture const& texture ) noexcept
{
  auto handle = texture.GetUAVHandle();
  Track( texture.GetTexture() );

  return handle;
}

Ember::SRVHandle Ember::ResourceBinder::BindTransient(
    Texture const& texture, CD3DX12_SHADER_RESOURCE_VIEW_DESC const& desc ) noexcept
{
  uint64_t hash = HashFnv1A( desc ) << DeviceHandleType::kSRV << texture.GetPtrID();

  if ( auto it = m_TempHandles.Find( hash ); it != m_TempHandles.end() )
  {
    return std::get<SRVHandle>( it->second );
  }

  Track( texture.GetTexture() );
  auto handle = m_Bindless->CreateDescriptorHandle( texture.GetTexture(), desc );
  m_TempHandles.Put( hash, handle );
  return handle;
}

Ember::UAVHandle Ember::ResourceBinder::BindTransient(
    Texture const& texture, CD3DX12_UNORDERED_ACCESS_VIEW_DESC const& desc ) noexcept
{
  uint64_t hash = HashFnv1A( desc ) << DeviceHandleType::kUAV << texture.GetPtrID();

  if ( auto it = m_TempHandles.Find( hash ); it != m_TempHandles.end() )
  {
    return std::get<UAVHandle>( it->second );
  }

  Track( texture.GetTexture() );
  auto handle = m_Bindless->CreateDescriptorHandle( texture.GetTexture(), desc );
  m_TempHandles.Put( hash, handle );
  return handle;
}

Ember::SRVHandle Ember::ResourceBinder::BindTransient(
    ComPtr<ID3D12Resource> resource, CD3DX12_SHADER_RESOURCE_VIEW_DESC const& desc ) noexcept
{
  uint64_t hash = HashFnv1A( desc ) << DeviceHandleType::kSRV << ( uintptr_t )resource.Get();

  if ( auto it = m_TempHandles.Find( hash ); it != m_TempHandles.end() )
  {
    return std::get<SRVHandle>( it->second );
  }

  Track( resource );
  auto handle = m_Bindless->CreateDescriptorHandle( resource.Get(), desc );
  m_TempHandles.Put( hash, handle );
  return handle;
}

Ember::UAVHandle Ember::ResourceBinder::BindTransient(
    ComPtr<ID3D12Resource> resource, CD3DX12_UNORDERED_ACCESS_VIEW_DESC const& desc ) noexcept
{
  uint64_t hash = HashFnv1A( desc ) << DeviceHandleType::kUAV << ( uintptr_t )resource.Get();

  if ( auto it = m_TempHandles.Find( hash ); it != m_TempHandles.end() )
  {
    return std::get<UAVHandle>( it->second );
  }

  Track( resource );
  auto handle = m_Bindless->CreateDescriptorHandle( resource.Get(), desc );
  m_TempHandles.Put( hash, handle );
  return handle;
}

void Ember::ResourceBinder::Track( ComPtr<IUnknown> resource ) noexcept
{
  m_UsedResources.insert( std::move( resource ) );
}

void Ember::ResourceBinder::Clear()
{
  if ( not m_Bindless ) return;

  m_UsedResources.clear();

  for ( auto const& handle : m_TempHandles.Values() )
  {
    std::visit( [&]( auto const& act_handle ) { m_Bindless->Free( act_handle ); }, handle );
  }

  m_TempHandles.Clear();
}

Ember::ResourceBinder::~ResourceBinder()
{
  if ( not m_Bindless ) return;

  for ( auto const& handle : m_TempHandles.Values() )
  {
    std::visit( [&]( auto const& act_handle ) { m_Bindless->Free( act_handle ); }, handle );
  }
}
