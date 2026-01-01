#include "ResourceBinder.hpp"

#include <algorithm>
#include "RenderDevice.hpp"

Ember::ResourceBinder::ResourceBinder( BindlessManager* bindless, std::pmr::polymorphic_allocator<> const& allocator )
  : m_Bindless{ bindless }, m_UsedTextures{ allocator }
{}

Ember::CBVHandle Ember::ResourceBinder::BindCBV( Buffer buffer ) noexcept
{
  auto handle = buffer.GetCBVHandle();
  m_UsedBuffers.push_front( std::move( buffer ) );

  return handle;
}

Ember::SRVHandle Ember::ResourceBinder::BindSRV( Buffer buffer ) noexcept
{
  auto handle = buffer.GetSRVHandle();
  m_UsedBuffers.push_front( std::move( buffer ) );

  return handle;
}

Ember::UAVHandle Ember::ResourceBinder::BindUAV( Buffer buffer ) noexcept
{
  auto handle = buffer.GetUAVHandle();
  m_UsedBuffers.push_front( std::move( buffer ) );

  return handle;
}

Ember::SRVHandle Ember::ResourceBinder::BindSRV( Texture texture ) noexcept
{
  auto handle = texture.GetSRVHandle();
  m_UsedTextures.push_front( std::move( texture ) );

  return handle;
}

Ember::UAVHandle Ember::ResourceBinder::BindUAV( Texture texture ) noexcept
{
  auto handle = texture.GetUAVHandle();
  m_UsedTextures.push_front( std::move( texture ) );

  return handle;
}

Ember::SRVHandle Ember::ResourceBinder::BindTransient(
    Texture texture, CD3DX12_SHADER_RESOURCE_VIEW_DESC const& desc ) noexcept
{
  uint64_t hash = HashFnv1A( desc ) << DeviceHandleType::kSRV << texture.GetPtrID();

  if ( auto it = m_TempHandles.Find( hash ); it != m_TempHandles.end() )
  {
    return std::get<SRVHandle>( it->second );
  }

  auto handle = m_Bindless->CreateDescriptorHandle( texture.GetTexture(), desc );
  m_TempHandles.Put( hash, handle );
  return handle;
}

Ember::UAVHandle Ember::ResourceBinder::BindTransient(
    Texture texture, CD3DX12_UNORDERED_ACCESS_VIEW_DESC const& desc ) noexcept
{
  uint64_t hash = HashFnv1A( desc ) << DeviceHandleType::kUAV << texture.GetPtrID();

  if ( auto it = m_TempHandles.Find( hash ); it != m_TempHandles.end() )
  {
    return std::get<UAVHandle>( it->second );
  }

  auto handle = m_Bindless->CreateDescriptorHandle( texture.GetTexture(), desc );
  m_TempHandles.Put( hash, handle );
  return handle;
}

void Ember::ResourceBinder::Clear()
{
  if ( not m_Bindless ) return;

  m_UsedBuffers.clear();
  m_UsedTextures.clear();

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
