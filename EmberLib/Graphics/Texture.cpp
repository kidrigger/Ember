#include "Texture.hpp"

#include "ScopedDeviceHandle.hpp"
#include "Util/DirectXHeaders.hpp"
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
  return ( bool )m_SamplerInfo->Handle;
}

Ember::SamplerHandle Ember::Sampler::GetSamplerHandle() const
{
  return m_SamplerInfo->Handle;
}

namespace Ember
{

struct TextureImpl
{
  ComPtr<ID3D12Resource>      Resource;
  ComPtr<D3D12MA::Allocation> Allocation;
  D3D12_RESOURCE_STATES       CurrentState;
  Texture::Type               Type;
  ScopedHandlePair            Handles;
};

} // namespace Ember

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

Ember::Texture::Type Ember::Texture::GetType() const noexcept
{
  return m_Impl->Type;
}

D3D12_RESOURCE_STATES Ember::Texture::GetCurrentState() const noexcept
{
  return m_Impl->CurrentState;
}

Ember::SRVHandle Ember::Texture::GetSRVHandle() const
{
  return m_Impl->Handles.GetSRV();
}

Ember::UAVHandle Ember::Texture::GetUAVHandle() const
{
  ASSERT( m_Impl->Type == Texture::Type::kStorage );
  auto uav = m_Impl->Handles.GetUAV();
  ASSERT( uav );
  return uav;
}

void Ember::Texture::SetName( LPCWSTR const name ) const
{
  ERR_ABORT( m_Impl->Resource->SetName( name ) );
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
      UNIMPLEMENTED_M( "Add formats as used/required" );
  }
}

Ember::Texture Ember::TextureManager::CreateDepthTexture2D( Tex2DDesc const& create_info )
{
  auto [format, width, height, usage, levels, array_size, init_state] = create_info;
  ASSERT( usage == TextureUsage::kDepthSample );

  ComPtr<ID3D12Resource>      texture;
  ComPtr<D3D12MA::Allocation> allocation;

  CD3DX12_RESOURCE_DESC       resource_desc = CD3DX12_RESOURCE_DESC::Tex2D( format, width, height, array_size, levels );
  resource_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

  //
  D3D12_CLEAR_VALUE clear_value = {
    .Format       = format,
    .DepthStencil = { .Depth = 1.0f, .Stencil = 0 },
  };

  CreateResourceImpl(
      &texture, &allocation, resource_desc, &clear_value, init_state.value_or( D3D12_RESOURCE_STATE_DEPTH_WRITE ) );

  DXGI_FORMAT const srv_format = MakeSRVCompat( format );
  SRVHandle         srv_handle = m_Bindless->CreateDescriptorHandle(
      texture.Get(),
      array_size == 1 ? CD3DX12_SHADER_RESOURCE_VIEW_DESC::Tex2D( srv_format )
                              : CD3DX12_SHADER_RESOURCE_VIEW_DESC::Tex2DArray( srv_format ) );

  return Texture{
    std::allocate_shared<TextureImpl>(
        GetAllocator(),
        TextureImpl{
                    .Resource     = std::move( texture ),
                    .Allocation   = std::move( allocation ),
                    .CurrentState = init_state.value_or( D3D12_RESOURCE_STATE_DEPTH_WRITE ),
                    .Type         = Texture::Type::kDepth,
                    .Handles      = { m_Bindless, srv_handle },
                    }
        )
  };
}

Ember::Texture Ember::TextureManager::CreateRenderTexture2D( Tex2DDesc const& create_info )
{
  auto [format, width, height, usage, levels, array_size, init_state] = create_info;
  ASSERT( usage == TextureUsage::kRenderTarget );

  ComPtr<ID3D12Resource>      texture;
  ComPtr<D3D12MA::Allocation> allocation;

  CD3DX12_RESOURCE_DESC       resource_desc = CD3DX12_RESOURCE_DESC::Tex2D( format, width, height, array_size, levels );
  resource_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

  //
  D3D12_CLEAR_VALUE const clear_value = {
    .Format = format,
    .Color  = { 0.0f, 0.0f, 0.0f, 0.0f },
  };

  CreateResourceImpl(
      &texture, &allocation, resource_desc, &clear_value, init_state.value_or( D3D12_RESOURCE_STATE_RENDER_TARGET ) );

  SRVHandle srv_handle = m_Bindless->CreateDescriptorHandle(
      texture.Get(),
      array_size == 1 ? CD3DX12_SHADER_RESOURCE_VIEW_DESC::Tex2D( format )
                      : CD3DX12_SHADER_RESOURCE_VIEW_DESC::Tex2DArray( format ) );

  return Texture{
    std::allocate_shared<TextureImpl>(
        GetAllocator(),
        TextureImpl{
                    .Resource     = std::move( texture ),
                    .Allocation   = std::move( allocation ),
                    .CurrentState = init_state.value_or( D3D12_RESOURCE_STATE_RENDER_TARGET ),
                    .Type         = Texture::Type::kAttachment,
                    .Handles      = { m_Bindless, srv_handle },
                    }
        )
  };
}

void Ember::TextureManager::CreateResourceImpl(
    ID3D12Resource**                       texture,
    [[maybe_unused]] D3D12MA::Allocation** allocation,
    CD3DX12_RESOURCE_DESC const&           resource_desc,
    D3D12_CLEAR_VALUE const*               clear_value,
    D3D12_RESOURCE_STATES                  initial_states ) const
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

Ember::Texture Ember::TextureManager::CreateTexture2D( Tex2DDesc const& create_info )
{
  auto [format, width, height, usage, levels, array_size, init_state] = create_info;
  // TODO: Check if still work keeping splits.
  if ( create_info.Usage == TextureUsage::kDepthSample )
  {
    return CreateDepthTexture2D( create_info );
  }
  if ( create_info.Usage == TextureUsage::kRenderTarget )
  {
    return CreateRenderTexture2D( create_info );
  }

  ComPtr<ID3D12Resource>      texture;
  ComPtr<D3D12MA::Allocation> allocation;

  CD3DX12_RESOURCE_DESC       resource_desc = CD3DX12_RESOURCE_DESC::Tex2D( format, width, height, array_size, levels );

  if ( usage == TextureUsage::kReadWrite ) resource_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  if ( usage == TextureUsage::kDepthSample ) resource_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

  CreateResourceImpl(
      &texture, &allocation, resource_desc, nullptr, init_state.value_or( D3D12_RESOURCE_STATE_COMMON ) );

  SRVHandle srv_handle = m_Bindless->CreateDescriptorHandle(
      texture.Get(),
      array_size == 1 ? CD3DX12_SHADER_RESOURCE_VIEW_DESC::Tex2D( format )
                      : CD3DX12_SHADER_RESOURCE_VIEW_DESC::Tex2DArray( format ) );

  switch ( usage )
  {
    case TextureUsage::kReadonly:
    {
      return Texture{
        std::allocate_shared<TextureImpl>(
            GetAllocator(),
            TextureImpl{
                        .Resource     = std::move( texture ),
                        .Allocation   = std::move( allocation ),
                        .CurrentState = init_state.value_or( D3D12_RESOURCE_STATE_COMMON ),
                        .Type         = Texture::Type::kSampled,
                        .Handles      = { m_Bindless, srv_handle },
                        }
            )
      };
    }
    case TextureUsage::kReadWrite:
    {
      UAVHandle uav_handle = m_Bindless->CreateDescriptorHandle(
          texture.Get(),
          array_size == 1 ? CD3DX12_UNORDERED_ACCESS_VIEW_DESC::Tex2D( DirectX::MakeLinear( format ) )
                          : CD3DX12_UNORDERED_ACCESS_VIEW_DESC::Tex2DArray( DirectX::MakeLinear( format ) ) );
      return Texture{
        std::allocate_shared<TextureImpl>(
            GetAllocator(),
            TextureImpl{
                        .Resource     = std::move( texture ),
                        .Allocation   = std::move( allocation ),
                        .CurrentState = init_state.value_or( D3D12_RESOURCE_STATE_COMMON ),
                        .Type         = Texture::Type::kStorage,
                        .Handles      = { m_Bindless, srv_handle, uav_handle },
                        }
            )
      };
    }
    default:
      UNREACHABLE_M( "The other case are dispatched at the start of the function" );
  }
}

Ember::Texture Ember::TextureManager::CreateDepthTextureCube( TexCubeDesc const& create_info )
{
  auto [format, side, usage, levels, init_state] = create_info;
  ASSERT( usage == TextureUsage::kDepthSample );

  ComPtr<ID3D12Resource>      texture;
  ComPtr<D3D12MA::Allocation> allocation;

  CD3DX12_RESOURCE_DESC       resource_desc  = CD3DX12_RESOURCE_DESC::Tex2D( format, side, side, 6, levels );
  resource_desc.Flags                       |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

  //
  D3D12_CLEAR_VALUE clear_value = {
    .Format       = format,
    .DepthStencil = { .Depth = 1.0f, .Stencil = 0 },
  };

  CreateResourceImpl(
      &texture, &allocation, resource_desc, &clear_value, init_state.value_or( D3D12_RESOURCE_STATE_COMMON ) );

  DXGI_FORMAT srv_format = MakeSRVCompat( format );
  SRVHandle   srv_handle =
      m_Bindless->CreateDescriptorHandle( texture.Get(), CD3DX12_SHADER_RESOURCE_VIEW_DESC::TexCube( srv_format ) );

  return Texture{
    std::allocate_shared<TextureImpl>(
        GetAllocator(),
        TextureImpl{
                    .Resource     = std::move( texture ),
                    .Allocation   = std::move( allocation ),
                    .CurrentState = init_state.value_or( D3D12_RESOURCE_STATE_COMMON ),
                    .Type         = Texture::Type::kDepth,
                    .Handles      = { m_Bindless, srv_handle },
                    }
        )
  };
}

Ember::Texture Ember::TextureManager::CreateTextureCube( TexCubeDesc const& create_info )
{
  auto [format, side, usage, levels, init_state] = create_info;
  if ( usage == TextureUsage::kDepthSample )
  {
    return CreateDepthTextureCube( create_info );
  }

  ComPtr<ID3D12Resource>      texture;
  ComPtr<D3D12MA::Allocation> allocation;

  CD3DX12_RESOURCE_DESC       resource_desc = CD3DX12_RESOURCE_DESC::Tex2D( format, side, side, 6, levels );

  if ( usage == TextureUsage::kReadWrite ) resource_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  if ( usage == TextureUsage::kDepthSample ) resource_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

  CreateResourceImpl(
      &texture, &allocation, resource_desc, nullptr, init_state.value_or( D3D12_RESOURCE_STATE_COMMON ) );

  SRVHandle srv_handle =
      m_Bindless->CreateDescriptorHandle( texture.Get(), CD3DX12_SHADER_RESOURCE_VIEW_DESC::TexCube( format ) );

  UAVHandle     uav_handle;
  Texture::Type type;
  switch ( usage )
  {
    case TextureUsage::kReadonly:
      type = Texture::Type::kSampled;
      break;
    case TextureUsage::kReadWrite:
      type       = Texture::Type::kStorage;
      uav_handle = m_Bindless->CreateDescriptorHandle(
          texture.Get(), CD3DX12_UNORDERED_ACCESS_VIEW_DESC::Tex2DArray( DirectX::MakeLinear( format ) ) );
      break;
    case TextureUsage::kDepthSample:
      type = Texture::Type::kDepth;
      break;
    default:
      UNREACHABLE;
  }
  return Texture{
    std::allocate_shared<TextureImpl>(
        GetAllocator(),
        TextureImpl{
                    .Resource     = std::move( texture ),
                    .Allocation   = std::move( allocation ),
                    .CurrentState = init_state.value_or( D3D12_RESOURCE_STATE_COMMON ),
                    .Type         = type,
                    .Handles      = { m_Bindless, srv_handle, uav_handle },
                    }
        )
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
