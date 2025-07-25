#include "Texture.hpp"

#include <DirectXTex.h>

#include "Util/HelperUtils.hpp"

Ember::Texture::TextureInfoImpl::TextureInfoImpl( BindlessManager* const bindless, SRVHandle as_srv, UAVHandle as_uav )
  : Bindless{ bindless }, AsSRV{ std::move( as_srv ) }, AsUAV{ std::move( as_uav ) }
{}

Ember::Texture::TextureInfoImpl::TextureInfoImpl( TextureInfoImpl&& other ) noexcept
  : Bindless{ other.Bindless }, AsSRV{ std::move( other.AsSRV ) }, AsUAV{ std::move( other.AsUAV ) }
{
  other.AsSRV = {};
  other.AsUAV = {};
}

Ember::Texture::TextureInfoImpl& Ember::Texture::TextureInfoImpl::operator=( TextureInfoImpl&& other ) noexcept
{
  if ( this == &other ) return *this;
  std::swap( Bindless, other.Bindless );
  std::swap( AsSRV, other.AsSRV );
  std::swap( AsUAV, other.AsUAV );
  return *this;
}

Ember::Texture::TextureInfoImpl::~TextureInfoImpl()
{
  if ( not Bindless ) return;

  Bindless->Free( AsSRV );
  Bindless->Free( AsUAV );
}

Ember::Texture::Texture(
    ComPtr<ID3D12Resource> texture, ComPtr<D3D12MA::Allocation> allocation, TextureInfo texture_info )
  : m_Texture{ std::move( texture ) }
  , m_Allocation{ std::move( allocation ) }
  , m_TextureInfo{ std::move( texture_info ) }
{}

ID3D12Resource* Ember::Texture::GetTexture() const
{
  return m_Texture.Get();
}

Ember::SRVHandle Ember::Texture::GetSRVHandle() const
{
  return m_TextureInfo->AsSRV;
}

Ember::UAVHandle Ember::Texture::GetUAVHandle() const
{
  return m_TextureInfo->AsUAV;
}

Ember::TextureManager::TextureManager(
    ComPtr<ID3D12Device2> device, ComPtr<D3D12MA::Allocator> gpu_allocator, BindlessManager* const bindless_manager )
  : m_Bindless{ bindless_manager }, m_Device{ std::move( device ) }, m_GpuAllocator{ std::move( gpu_allocator ) }
{}

Ember::Texture Ember::TextureManager::CreateTexture2D(
    DXGI_FORMAT const format, uint32_t const width, uint32_t const height )
{

  ComPtr<ID3D12Resource>      texture;
  ComPtr<D3D12MA::Allocation> allocation;

  CD3DX12_RESOURCE_DESC       resource_desc = CD3DX12_RESOURCE_DESC::Tex2D( format, width, height );
  resource_desc.Flags                       = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
#if not defined( RENDERDOC_COMPAT )
  D3D12MA::ALLOCATION_DESC const allocation_desc = {
    .Flags    = D3D12MA::ALLOCATION_FLAG_NONE,
    .HeapType = D3D12_HEAP_TYPE_DEFAULT,
  };

  ERR_ABORT( m_GpuAllocator->CreateResource(
      &allocation_desc,
      &resource_desc,
      D3D12_RESOURCE_STATE_COPY_DEST,
      nullptr,
      &allocation,
      IID_PPV_ARGS( &texture ) ) );
#else
  auto const heap_properties = CD3DX12_HEAP_PROPERTIES{ D3D12_HEAP_TYPE_DEFAULT };
  ERR_ABORT( m_Device->CreateCommittedResource(
      &heap_properties,
      D3D12_HEAP_FLAG_NONE,
      &resource_desc,
      D3D12_RESOURCE_STATE_COPY_DEST,
      nullptr,
      IID_PPV_ARGS( &texture ) ) );
#endif

  SRVHandle srv_handle =
      m_Bindless->CreateDescriptorHandle( texture.Get(), CD3DX12_SHADER_RESOURCE_VIEW_DESC::Tex2D( format ) );
  UAVHandle uav_handle = m_Bindless->CreateDescriptorHandle(
      texture.Get(), CD3DX12_UNORDERED_ACCESS_VIEW_DESC::Tex2D( DirectX::MakeLinear( format ) ) );

  auto texture_info = std::allocate_shared<Texture::TextureInfoImpl>(
      std::pmr::polymorphic_allocator<byte>{ &m_MemoryPool }, m_Bindless, srv_handle, uav_handle );

  return Texture{ std::move( texture ), std::move( allocation ), std::move( texture_info ) };
}
