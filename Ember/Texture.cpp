#include "Texture.hpp"

#include <DirectXTex.h>

#include "Util/HelperUtils.hpp"

Ember::Sampler::SamplerInfoImpl::SamplerInfoImpl( BindlessManager* const bindless, SamplerHandle handle )
  : Bindless{ bindless }, Handle{ std::move( handle ) }
{}

Ember::Sampler::SamplerInfoImpl::SamplerInfoImpl( SamplerInfoImpl&& other ) noexcept
  : Bindless{ other.Bindless }, Handle{ std::move( other.Handle ) }
{
  other.Bindless = nullptr;
  other.Handle   = {};
}

Ember::Sampler::SamplerInfoImpl& Ember::Sampler::SamplerInfoImpl::operator=( SamplerInfoImpl&& other ) noexcept
{
  if ( this == &other ) return *this;
  std::swap( Bindless, other.Bindless );
  std::swap( Handle, other.Handle );
  return *this;
}

Ember::Sampler::SamplerInfoImpl::~SamplerInfoImpl()
{
  if ( not Bindless ) return;

  Bindless->Free( Handle );
}

Ember::Sampler::Sampler( SamplerInfo sampler_handle ) : m_SamplerInfo{ std::move( sampler_handle ) }
{}

Ember::Sampler::operator bool() const
{
  return m_SamplerInfo->Handle;
}

Ember::SamplerHandle Ember::Sampler::GetSamplerHandle() const
{
  return m_SamplerInfo->Handle;
}

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

Ember::Texture::operator bool() const
{
  return m_Texture;
}

ID3D12Resource* Ember::Texture::GetTexture() const
{
  return m_Texture.Get();
}

D3D12MA::Allocation* Ember::Texture::GetAllocation() const
{
  return m_Allocation.Get();
}

Ember::SRVHandle Ember::Texture::GetSRVHandle() const
{
  ASSERT( m_TextureInfo and m_TextureInfo->AsSRV );
  return m_TextureInfo->AsSRV;
}

Ember::UAVHandle Ember::Texture::GetUAVHandle() const
{
  ASSERT( m_TextureInfo and m_TextureInfo->AsUAV );
  return m_TextureInfo->AsUAV;
}

Ember::TextureManager::TextureManager(
    ComPtr<ID3D12Device2> device, ComPtr<D3D12MA::Allocator> allocator, BindlessManager* const bindless_manager )
  : m_Bindless{ bindless_manager }, m_Device{ std::move( device ) }, m_Allocator{ std::move( allocator ) }
{}

Ember::Texture Ember::TextureManager::CreateTexture2D(
    DXGI_FORMAT const format, uint32_t const width, uint32_t const height )
{
  ComPtr<ID3D12Resource>      texture;
  ComPtr<D3D12MA::Allocation> allocation;

  CD3DX12_RESOURCE_DESC       resource_desc = CD3DX12_RESOURCE_DESC::Tex2D( format, width, height );
#if not defined( RENDERDOC_COMPAT )
  D3D12MA::ALLOCATION_DESC const allocation_desc = {
    .Flags    = D3D12MA::ALLOCATION_FLAG_NONE,
    .HeapType = D3D12_HEAP_TYPE_DEFAULT,
  };

  ERR_ABORT( m_Allocator->CreateResource(
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
  auto texture_info = std::allocate_shared<Texture::TextureInfoImpl>(
      std::pmr::polymorphic_allocator{ &m_MemoryPool }, m_Bindless, srv_handle, UAVHandle{} );

  return Texture{ std::move( texture ), std::move( allocation ), std::move( texture_info ) };
}

Ember::Texture Ember::TextureManager::CreateReadWriteTexture2D( DXGI_FORMAT format, uint32_t width, uint32_t height )
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

  ERR_ABORT( m_Allocator->CreateResource(
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
      std::pmr::polymorphic_allocator{ &m_MemoryPool }, m_Bindless, srv_handle, uav_handle );

  return Texture{ std::move( texture ), std::move( allocation ), std::move( texture_info ) };
}

Ember::Sampler Ember::TextureManager::CreateSampler( D3D12_SAMPLER_DESC const& sampler_desc )
{
  SamplerHandle handle = m_Bindless->CreateSamplerHandle( sampler_desc );

  return Sampler{ std::allocate_shared<Sampler::SamplerInfoImpl>(
      std::pmr::polymorphic_allocator{ &m_MemoryPool }, m_Bindless, handle ) };
}
