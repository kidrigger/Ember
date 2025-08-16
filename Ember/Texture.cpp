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
  return ( bool )m_SamplerInfo->Handle;
}

Ember::SamplerHandle Ember::Sampler::GetSamplerHandle() const
{
  return m_SamplerInfo->Handle;
}

Ember::Texture::SampledInfoImpl::SampledInfoImpl( BindlessManager* const bindless, SRVHandle as_srv )
  : Bindless{ bindless }, AsSRV{ std::move( as_srv ) }
{}

Ember::Texture::SampledInfoImpl::SampledInfoImpl( SampledInfoImpl&& other ) noexcept
  : Bindless{ other.Bindless }, AsSRV{ std::move( other.AsSRV ) }
{
  other.Bindless = nullptr;
  other.AsSRV    = {};
}

Ember::Texture::SampledInfoImpl& Ember::Texture::SampledInfoImpl::operator=( SampledInfoImpl&& other ) noexcept
{
  if ( this == &other ) return *this;
  std::swap( Bindless, other.Bindless );
  std::swap( AsSRV, other.AsSRV );
  return *this;
}

Ember::Texture::SampledInfoImpl::~SampledInfoImpl()
{
  if ( not Bindless ) return;

  Bindless->Free( AsSRV );
}

Ember::Texture::StorageInfoImpl::StorageInfoImpl( BindlessManager* const bindless, SRVHandle as_srv, UAVHandle as_uav )
  : Bindless{ bindless }, AsSRV{ std::move( as_srv ) }, AsUAV{ std::move( as_uav ) }
{}

Ember::Texture::StorageInfoImpl::StorageInfoImpl( StorageInfoImpl&& other ) noexcept
  : Bindless{ other.Bindless }, AsSRV{ std::move( other.AsSRV ) }, AsUAV{ std::move( other.AsUAV ) }
{
  other.AsSRV = {};
  other.AsUAV = {};
}

Ember::Texture::StorageInfoImpl& Ember::Texture::StorageInfoImpl::operator=( StorageInfoImpl&& other ) noexcept
{
  if ( this == &other ) return *this;
  std::swap( Bindless, other.Bindless );
  std::swap( AsSRV, other.AsSRV );
  std::swap( AsUAV, other.AsUAV );
  return *this;
}

Ember::Texture::StorageInfoImpl::~StorageInfoImpl()
{
  if ( not Bindless ) return;

  Bindless->Free( AsSRV );
  Bindless->Free( AsUAV );
}

Ember::Texture::DepthInfoImpl::DepthInfoImpl(
    BindlessManager* const bindless, SRVHandle as_srv, D3D12_DEPTH_STENCIL_VIEW_DESC view )
  : Bindless{ bindless }, AsSRV{ std::move( as_srv ) }, AsDSVDesc{ std::move( view ) }
{}

Ember::Texture::DepthInfoImpl::DepthInfoImpl( DepthInfoImpl&& other ) noexcept
  : Bindless{ other.Bindless }, AsSRV{ std::move( other.AsSRV ) }, AsDSVDesc{ std::move( other.AsDSVDesc ) }
{
  other.Bindless  = nullptr;
  other.AsSRV     = {};
  other.AsDSVDesc = {};
}

Ember::Texture::DepthInfoImpl& Ember::Texture::DepthInfoImpl::operator=( DepthInfoImpl&& other ) noexcept
{
  if ( this == &other ) return *this;
  std::swap( Bindless, other.Bindless );
  std::swap( AsSRV, other.AsSRV );
  std::swap( AsDSVDesc, other.AsDSVDesc );
  return *this;
}

Ember::Texture::DepthInfoImpl::~DepthInfoImpl()
{
  if ( not Bindless ) return;

  Bindless->Free( AsSRV );
}

Ember::Texture::AttachmentInfoImpl::AttachmentInfoImpl(
    BindlessManager* const bindless, SRVHandle as_srv, D3D12_RENDER_TARGET_VIEW_DESC as_rtv_desc )
  : Bindless{ bindless }, AsSRV{ std::move( as_srv ) }, AsRTVDesc{ std::move( as_rtv_desc ) }
{}

Ember::Texture::AttachmentInfoImpl::AttachmentInfoImpl( AttachmentInfoImpl&& other ) noexcept
  : Bindless{ other.Bindless }, AsSRV{ std::move( other.AsSRV ) }, AsRTVDesc{ std::move( other.AsRTVDesc ) }
{
  other.Bindless  = nullptr;
  other.AsSRV     = {};
  other.AsRTVDesc = {};
}

Ember::Texture::AttachmentInfoImpl& Ember::Texture::AttachmentInfoImpl::operator=( AttachmentInfoImpl&& other ) noexcept
{
  if ( this == &other ) return *this;
  std::swap( Bindless, other.Bindless );
  std::swap( AsSRV, other.AsSRV );
  std::swap( AsRTVDesc, other.AsRTVDesc );
  return *this;
}

Ember::Texture::AttachmentInfoImpl::~AttachmentInfoImpl()
{
  if ( not Bindless ) return;

  Bindless->Free( AsSRV );
}

Ember::Texture::Texture( ComPtr<ID3D12Resource> texture, ComPtr<D3D12MA::Allocation> allocation, Views views )
  : m_Texture{ std::move( texture ) }, m_Allocation{ std::move( allocation ) }, m_Views{ std::move( views ) }
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

Ember::Texture::Type Ember::Texture::GetType() const noexcept
{
  return ( Type )m_Views.index();
}

Ember::SRVHandle Ember::Texture::GetSRVHandle() const
{
  ASSERT( m_Texture );
  switch ( GetType() )
  {
    case Type::kSampled:
      return std::get<SampledInfo>( m_Views )->AsSRV;
    case Type::kStorage:
      return std::get<StorageInfo>( m_Views )->AsSRV;
    case Type::kDepth:
      return std::get<DepthInfo>( m_Views )->AsSRV;
    case Type::kAttachment:
      return std::get<AttachmentInfo>( m_Views )->AsSRV;
  }
  UNREACHABLE;
}

Ember::UAVHandle Ember::Texture::GetUAVHandle() const
{
  ASSERT( m_Texture );
  ASSERT( GetType() == Type::kStorage );

  return std::get<StorageInfo>( m_Views )->AsUAV;
}

D3D12_DEPTH_STENCIL_VIEW_DESC const* Ember::Texture::GetDepthStencilView() const
{
  ASSERT( m_Texture );
  ASSERT( GetType() == Type::kDepth );

  return &std::get<DepthInfo>( m_Views )->AsDSVDesc;
}

D3D12_RENDER_TARGET_VIEW_DESC const* Ember::Texture::GetRenderTargetView() const
{
  ASSERT( m_Texture );
  ASSERT( GetType() == Type::kAttachment );

  return &std::get<AttachmentInfo>( m_Views )->AsRTVDesc;
}

void Ember::Texture::SetName( LPCWSTR const name ) const
{
  ERR_ABORT( m_Texture->SetName( name ) );
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

Ember::Texture Ember::TextureManager::CreateDepthTexture2D( Texture2DCreateInfo const& create_info )
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

  DXGI_FORMAT srv_format = MakeSRVCompat( format );
  SRVHandle   srv_handle =
      m_Bindless->CreateDescriptorHandle( texture.Get(), CD3DX12_SHADER_RESOURCE_VIEW_DESC::Tex2D( srv_format ) );

  D3D12_DEPTH_STENCIL_VIEW_DESC depth_stencil_view{
    .Format = format,
    .Flags  = D3D12_DSV_FLAG_NONE,
  };
  if ( array_size == 1 )
  {
    depth_stencil_view.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    depth_stencil_view.Texture2D     = { .MipSlice = 0 };
  }
  else
  {
    depth_stencil_view.ViewDimension  = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
    depth_stencil_view.Texture2DArray = { .MipSlice = 0, .FirstArraySlice = 0, .ArraySize = array_size };
  }

  auto texture_info = std::allocate_shared<Texture::DepthInfoImpl>(
      std::pmr::polymorphic_allocator{ &m_MemoryPool }, m_Bindless, srv_handle, depth_stencil_view );

  return Texture{ std::move( texture ), std::move( allocation ), std::move( texture_info ) };
}

Ember::Texture Ember::TextureManager::CreateRenderTexture2D( Texture2DCreateInfo const& create_info )
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

  SRVHandle srv_handle =
      m_Bindless->CreateDescriptorHandle( texture.Get(), CD3DX12_SHADER_RESOURCE_VIEW_DESC::Tex2D( format ) );

  D3D12_RENDER_TARGET_VIEW_DESC render_target_view{
    .Format = format,
  };

  if ( array_size == 1 )
  {
    render_target_view.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    render_target_view.Texture2D     = { .MipSlice = 0, .PlaneSlice = 0 };
  }
  else
  {
    render_target_view.ViewDimension  = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
    render_target_view.Texture2DArray = {
      .MipSlice        = 0,
      .FirstArraySlice = 0,
      .ArraySize       = array_size,
      .PlaneSlice      = 0,
    };
  }

  auto texture_info = std::allocate_shared<Texture::AttachmentInfoImpl>(
      std::pmr::polymorphic_allocator{ &m_MemoryPool }, m_Bindless, srv_handle, render_target_view );

  return Texture{ std::move( texture ), std::move( allocation ), std::move( texture_info ) };
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

Ember::Texture Ember::TextureManager::CreateTexture2D( Texture2DCreateInfo const& create_info )
{
  auto [format, width, height, usage, levels, array_size, init_state] = create_info;
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
      auto texture_info = std::allocate_shared<Texture::SampledInfoImpl>(
          std::pmr::polymorphic_allocator{ &m_MemoryPool }, m_Bindless, srv_handle );

      return Texture{ std::move( texture ), std::move( allocation ), std::move( texture_info ) };
    }
    case TextureUsage::kReadWrite:
    {
      auto      uav_format = DirectX::MakeLinear( format );
      UAVHandle uav_handle = m_Bindless->CreateDescriptorHandle(
          texture.Get(),
          array_size == 1 ? CD3DX12_UNORDERED_ACCESS_VIEW_DESC::Tex2D( uav_format )
                          : CD3DX12_UNORDERED_ACCESS_VIEW_DESC::Tex2DArray( uav_format ) );

      auto texture_info = std::allocate_shared<Texture::StorageInfoImpl>(
          std::pmr::polymorphic_allocator{ &m_MemoryPool }, m_Bindless, srv_handle, uav_handle );

      return Texture{ std::move( texture ), std::move( allocation ), std::move( texture_info ) };
    }
    default:
      UNREACHABLE_M( "The other case are dispatched at the start of the function" );
  }
}

Ember::Texture Ember::TextureManager::CreateDepthTextureCube( TextureCubeCreateInfo const& create_info )
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

  D3D12_DEPTH_STENCIL_VIEW_DESC depth_stencil_view{
    .Format         = format,
    .ViewDimension  = D3D12_DSV_DIMENSION_TEXTURE2DARRAY,
    .Flags          = D3D12_DSV_FLAG_NONE,
    .Texture2DArray = { .MipSlice = 0, .FirstArraySlice = 0, .ArraySize = 6 },
  };

  auto texture_info = std::allocate_shared<Texture::DepthInfoImpl>(
      std::pmr::polymorphic_allocator{ &m_MemoryPool }, m_Bindless, srv_handle, depth_stencil_view );

  return Texture{ std::move( texture ), std::move( allocation ), std::move( texture_info ) };
}

Ember::Texture Ember::TextureManager::CreateTextureCube( TextureCubeCreateInfo const& create_info )
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

  switch ( usage )
  {
    case TextureUsage::kReadonly:
    {
      auto texture_info = std::allocate_shared<Texture::SampledInfoImpl>(
          std::pmr::polymorphic_allocator{ &m_MemoryPool }, m_Bindless, srv_handle );

      return Texture{ std::move( texture ), std::move( allocation ), std::move( texture_info ) };
    }
    case TextureUsage::kReadWrite:
    {
      UAVHandle uav_handle = m_Bindless->CreateDescriptorHandle(
          texture.Get(), CD3DX12_UNORDERED_ACCESS_VIEW_DESC::Tex2DArray( DirectX::MakeLinear( format ) ) );

      auto texture_info = std::allocate_shared<Texture::StorageInfoImpl>(
          std::pmr::polymorphic_allocator{ &m_MemoryPool }, m_Bindless, srv_handle, uav_handle );

      return Texture{ std::move( texture ), std::move( allocation ), std::move( texture_info ) };
    }
    case TextureUsage::kDepthSample:
    {
      D3D12_DEPTH_STENCIL_VIEW_DESC depth_stencil_view{
        .Format         = format,
        .ViewDimension  = D3D12_DSV_DIMENSION_TEXTURE2DARRAY,
        .Flags          = D3D12_DSV_FLAG_NONE,
        .Texture2DArray = { .MipSlice = 0, .FirstArraySlice = 0, .ArraySize = 6 },
      };

      auto texture_info = std::allocate_shared<Texture::DepthInfoImpl>(
          std::pmr::polymorphic_allocator{ &m_MemoryPool }, m_Bindless, srv_handle, depth_stencil_view );

      return Texture{ std::move( texture ), std::move( allocation ), std::move( texture_info ) };
    }
  }
  UNREACHABLE;
}

Ember::Sampler Ember::TextureManager::CreateSampler( D3D12_SAMPLER_DESC const& sampler_desc )
{
  SamplerHandle handle = m_Bindless->CreateSamplerHandle( sampler_desc );

  return Sampler{ std::allocate_shared<Sampler::SamplerInfoImpl>(
      std::pmr::polymorphic_allocator{ &m_MemoryPool }, m_Bindless, handle ) };
}
