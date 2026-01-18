#include "Texture.hpp"

#include "ScopedDeviceHandle.hpp"
#include "Util/DirectXHeaders.hpp"
#include "Util/HelperUtils.hpp"

Ember::HashFnv1A Ember::TextureDesc::Hash() const
{
  return HashFnv1A{ Format } << Width << Height << MipLevels << ArraySize << ( uint32_t )Usage << ( uint32_t )Dim
                             << InitState.has_value() << InitState.value_or( D3D12_RESOURCE_STATE_COMMON );
}

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
  return ( bool )m_SamplerInfo->Handle;
}

Ember::SamplerHandle Ember::Sampler::GetSamplerHandle() const
{
  return m_SamplerInfo->Handle;
}

namespace
{

D3D12_RESOURCE_STATES DefaultInitStateFor( Ember::TextureUsage const usage )
{
  switch ( usage )
  {
    case Ember::TextureUsage::kReadonly:
      [[fallthrough]];
    case Ember::TextureUsage::kReadWrite:
      return D3D12_RESOURCE_STATE_COMMON;
    case Ember::TextureUsage::kDepthStencil:
      return D3D12_RESOURCE_STATE_DEPTH_WRITE;
    case Ember::TextureUsage::kRenderTarget:
      return D3D12_RESOURCE_STATE_RENDER_TARGET;
    default:
      UNIMPLEMENTED_M( "Unknown TextureUsage" );
  }
}
} // namespace

Ember::Tex2DDesc::operator Ember::TextureDesc() const
{
  return TextureDesc{
    .Format    = Format,
    .Width     = Width,
    .Height    = Height,
    .MipLevels = MipLevels,
    .ArraySize = ArraySize,
    .Usage     = Usage,
    .Dim       = TextureDim::k2D,
    .InitState = InitState.value_or( DefaultInitStateFor( Usage ) ),
  };
}

Ember::TexCubeDesc::operator Ember::TextureDesc() const
{
  return TextureDesc{
    .Format    = Format,
    .Width     = Side,
    .Height    = Side,
    .MipLevels = MipLevels,
    .ArraySize = 1,
    .Usage     = Usage,
    .Dim       = TextureDim::kCube,
    .InitState = InitState.value_or( DefaultInitStateFor( Usage ) ),
  };
}

Ember::Texture::Texture( std::shared_ptr<TextureImpl> impl ) : m_Impl{ std::move( impl ) }
{}

Ember::Texture::operator bool() const
{
  return ( bool )m_Impl;
}

ID3D12Resource* Ember::Texture::GetTexture() const
{
  return m_Impl->Resource.Get();
}

D3D12MA::Allocation* Ember::Texture::GetAllocation() const
{
  return m_Impl->Allocation.Get();
}

D3D12_RESOURCE_STATES Ember::Texture::GetCurrentState() const noexcept
{
  return m_Impl->CurrentState;
}

void Ember::Texture::SetCurrentState( D3D12_RESOURCE_STATES const state ) const noexcept
{
  m_Impl->CurrentState = state;
}

Ember::SRVHandle Ember::Texture::GetSRVHandle() const
{
  return m_Impl->Handles.GetSRV();
}

Ember::UAVHandle Ember::Texture::GetUAVHandle() const
{
  ASSERT( m_Impl->Desc.Usage == Texture::Type::kReadWrite );
  auto uav = m_Impl->Handles.GetUAV();
  ASSERT( uav );
  return uav;
}

uintptr_t Ember::Texture::GetPtrID() const
{
  return ( uintptr_t )m_Impl.get();
}

void Ember::Texture::SetName( LPCWSTR const name ) const
{
  ERR_ABORT( m_Impl->Resource->SetName( name ) );
}

Ember::Texture::Desc const& Ember::Texture::GetDesc() const noexcept
{
  return m_Impl->Desc;
}

Ember::MipLevels::MipLevels( uint16_t const levels ) : m_Value{ levels }
{}

Ember::MipLevels::operator UINT16() const
{
  return m_Value;
}

Ember::TextureManager::TextureManager(
    ComPtr<ID3D12Device2> device, ComPtr<D3D12MA::Allocator> allocator, BindlessManager* const bindless_manager )
  : m_Bindless{ bindless_manager }, m_Device{ std::move( device ) }, m_Allocator{ std::move( allocator ) }
{}

DXGI_FORMAT MakeSRVCompat( DXGI_FORMAT const format )
{
  switch ( format )
  {
    case DXGI_FORMAT_D32_FLOAT:
      return DXGI_FORMAT_R32_FLOAT;
    case DXGI_FORMAT_D16_UNORM:
      return DXGI_FORMAT_R16_UNORM;
    case DXGI_FORMAT_D24_UNORM_S8_UINT:
      return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
      return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
    default:
      return format;
      // UNIMPLEMENTED_M( "Add formats as used/required" );
  }
}

void Ember::TextureManager::CreateResourceImpl(
    ID3D12Resource**                       texture,
    [[maybe_unused]] D3D12MA::Allocation** allocation,
    CD3DX12_RESOURCE_DESC const&           resource_desc,
    D3D12_CLEAR_VALUE const*               clear_value,
    D3D12_RESOURCE_STATES const            initial_states ) const
{
#if not defined( RENDERDOC_COMPAT )
  D3D12MA::ALLOCATION_DESC const allocation_desc = {
    .Flags    = D3D12MA::ALLOCATION_FLAG_NONE,
    .HeapType = D3D12_HEAP_TYPE_DEFAULT,
  };

  ERR_ABORT( m_Allocator->CreateResource(
      &allocation_desc, &resource_desc, initial_states, clear_value, allocation, IID_PPV_ARGS( texture ) ) );
#else
  auto const heap_properties = CD3DX12_HEAP_PROPERTIES{ D3D12_HEAP_TYPE_DEFAULT };
  ERR_ABORT( m_Device->CreateCommittedResource(
      &heap_properties, D3D12_HEAP_FLAG_NONE, &resource_desc, initial_states, clear_value, IID_PPV_ARGS( texture ) ) );
#endif
}

std::shared_ptr<Ember::TextureImpl> Ember::TextureManager::CreateTextureImpl( TextureDesc const& desc )
{
  ASSERT( desc.ArraySize > 0 );
  ASSERT( desc.Format != DXGI_FORMAT_UNKNOWN );

  ComPtr<ID3D12Resource>      texture;
  ComPtr<D3D12MA::Allocation> allocation;

  uint16_t const              actual_array_size = desc.Dim == TextureDim::kCube ? desc.ArraySize * 6 : desc.ArraySize;

  CD3DX12_RESOURCE_DESC       resource_desc =
      CD3DX12_RESOURCE_DESC::Tex2D( desc.Format, desc.Width, desc.Height, actual_array_size, desc.MipLevels );

  switch ( desc.Usage )
  {
    case TextureUsage::kReadonly:
      break;
    case TextureUsage::kReadWrite:
      resource_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
      break;
    case TextureUsage::kDepthStencil:
      resource_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
      break;
    case TextureUsage::kRenderTarget:
      resource_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
      break;
    default:
      UNIMPLEMENTED_M( "Unknown TextureUsage" );
  }

  //
  std::optional<D3D12_CLEAR_VALUE> clear_value;
  switch ( desc.Usage )
  {
    case TextureUsage::kReadonly:
      break;
    case TextureUsage::kReadWrite:
      break;
    case TextureUsage::kDepthStencil:
      clear_value = {
        .Format       = desc.Format,
        .DepthStencil = { .Depth = 1.0f, .Stencil = 0 },
      };
      break;
    case TextureUsage::kRenderTarget:
      clear_value = {
        .Format = desc.Format,
        .Color  = { 0.0f, 0.0f, 0.0f, 0.0f },
      };
      break;
  }

  auto current_state = desc.InitState.value_or( DefaultInitStateFor( desc.Usage ) );

  CreateResourceImpl(
      &texture, &allocation, resource_desc, clear_value ? &clear_value.value() : nullptr, current_state );

  CD3DX12_SHADER_RESOURCE_VIEW_DESC srv_desc;
  switch ( desc.Dim )
  {
    case TextureDim::k2D:
      srv_desc = desc.ArraySize > 1
                     ? CD3DX12_SHADER_RESOURCE_VIEW_DESC::Tex2DArray( MakeSRVCompat( desc.Format ), desc.ArraySize )
                     : CD3DX12_SHADER_RESOURCE_VIEW_DESC::Tex2D( MakeSRVCompat( desc.Format ) );
      break;
    case TextureDim::kCube:
      srv_desc = desc.ArraySize > 1
                     ? CD3DX12_SHADER_RESOURCE_VIEW_DESC::TexCubeArray( MakeSRVCompat( desc.Format ), desc.ArraySize )
                     : CD3DX12_SHADER_RESOURCE_VIEW_DESC::TexCube( MakeSRVCompat( desc.Format ) );
      break;
  }

  SRVHandle srv_handle = m_Bindless->CreateDescriptorHandle( texture.Get(), srv_desc );

  UAVHandle uav_handle = {};
  if ( desc.Usage == TextureUsage::kReadWrite )
  {
    auto const uav_format = DirectX::MakeLinear( desc.Format );
    auto const uav_desc   = actual_array_size > 1
                                ? CD3DX12_UNORDERED_ACCESS_VIEW_DESC::Tex2DArray( uav_format, actual_array_size )
                                : CD3DX12_UNORDERED_ACCESS_VIEW_DESC::Tex2D( uav_format );
    uav_handle            = m_Bindless->CreateDescriptorHandle( texture.Get(), uav_desc );
  }

  return std::allocate_shared<TextureImpl>(
      GetAllocator(),
      std::move( texture ),
      std::move( allocation ),
      current_state,
      ScopedHandlePair{ m_Bindless, srv_handle, uav_handle },
      desc );
}

Ember::Texture Ember::TextureManager::CreateTexture2D( Tex2DDesc const& create_info )
{
  return Texture{ CreateTextureImpl( ( TextureDesc )create_info ) };
}

Ember::Texture Ember::TextureManager::CreateTextureCube( TexCubeDesc const& create_info )
{
  return Texture{ CreateTextureImpl( ( TextureDesc )create_info ) };
}

Ember::Texture Ember::TextureManager::CreateTexture( TextureDesc const& desc )
{
  return Texture{ CreateTextureImpl( desc ) };
}

Ember::Texture Ember::TextureManager::ImportTexture( ComPtr<ID3D12Resource> resource, TextureDesc const& desc )
{
  ASSERT_M( desc.Usage == TextureUsage::kRenderTarget, "Import Texture is specifically for Backbuffer." );
  return Texture{
    std::allocate_shared<TextureImpl>(
        GetAllocator(), std::move( resource ), nullptr, desc.InitState.value(), ScopedHandlePair{}, desc ),
  };
}

Ember::Sampler Ember::TextureManager::CreateSampler( D3D12_SAMPLER_DESC const& sampler_desc )
{
  SamplerHandle handle = m_Bindless->CreateSamplerHandle( sampler_desc );

  return Sampler{ std::allocate_shared<Sampler::SamplerInfoImpl>( GetAllocator(), m_Bindless, handle ) };
}

inline std::pmr::polymorphic_allocator<> Ember::TextureManager::GetAllocator() noexcept
{
  return std::pmr::polymorphic_allocator<>{ &m_MemoryPool };
}
