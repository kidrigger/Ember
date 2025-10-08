#include "FrameGraphHelper.hpp"

#include "RenderDevice.hpp"
#include "RenderTargetManager.hpp"
#include "Util/DataUtil.hpp"
#include "Util/HelperUtils.hpp"

#pragma comment( lib, "FrameGraph.lib" )

Ember::FG::Texture Ember::FG::Context::CreateTextureImpl( Texture::Desc const& desc ) const
{
  auto [format, width, height, levels, array_size, init_state, flags] = desc;

  ComPtr<ID3D12Resource>      texture;
  ComPtr<D3D12MA::Allocation> allocation;

  auto resource_desc = CD3DX12_RESOURCE_DESC::Tex2D( format, width, height, array_size, levels, 1, 0, flags );

  std::optional<D3D12_CLEAR_VALUE> clear_value;
  if ( flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL )
  {
    clear_value = {
      .Format       = format,
      .DepthStencil = { .Depth = 1.0f, .Stencil = 0 },
    };
  }
  else if ( flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET )
  {
    clear_value = {
      .Format = format,
      .Color  = { 0.0f, 0.0f, 0.0f, 0.0f },
    };
  }

#if not defined( RENDERDOC_COMPAT )
  D3D12MA::ALLOCATION_DESC const allocation_desc = {
    .Flags    = D3D12MA::ALLOCATION_FLAG_NONE,
    .HeapType = D3D12_HEAP_TYPE_DEFAULT,
  };

  D3D12MA::Allocator* allocator = m_RenderDevice->GetAllocator();

  ERR_ABORT( allocator->CreateResource(
      &allocation_desc,
      &resource_desc,
      init_state,
      clear_value.has_value() ? &clear_value.value() : nullptr,
      &allocation,
      IID_PPV_ARGS( &texture ) ) );
#else
  auto const     heap_properties = CD3DX12_HEAP_PROPERTIES{ D3D12_HEAP_TYPE_DEFAULT };

  ID3D12Device2* device          = m_RenderDevice->GetDevice();

  ERR_ABORT( device->CreateCommittedResource(
      &heap_properties,
      D3D12_HEAP_FLAG_NONE,
      &resource_desc,
      init_state,
      clear_value.has_value() ? &clear_value.value() : nullptr,
      IID_PPV_ARGS( &texture ) ) );
#endif

  return Texture{
    .Resource     = std::move( texture ),
    .Allocation   = std::move( allocation ),
    .CurrentState = init_state,
  };
}

Ember::FG::Context::Context( RenderDevice* render_device, std::unique_ptr<RenderTargetManager> render_target_manager )
  : m_RenderDevice{ render_device }, m_RenderTargetManager{ std::move( render_target_manager ) }, m_FrameData{}
{}

void Ember::FG::Context::Create( Context* out, RenderDevice* render_device )
{
  auto render_target_manager = std::make_unique_for_overwrite<RenderTargetManager>();
  RenderTargetManager::Create( render_target_manager.get(), render_device );

  new ( out ) Context{ render_device, std::move( render_target_manager ) };
}

Ember::RenderDevice* Ember::FG::Context::GetRenderDevice() const
{
  return m_RenderDevice;
}

Ember::FG::Context::FrameData const& Ember::FG::Context::GetFrameData() const
{
  return m_FrameData;
}

void Ember::FG::Context::SetFrameData( FrameData const& frame_data )
{
  m_FrameData = frame_data;
}

Ember::RenderTargetManager* Ember::FG::Context::GetRenderTargetManager() const
{
  return m_RenderTargetManager.get();
}

void Ember::FG::Context::PushBarrier( CD3DX12_RESOURCE_BARRIER const& barrier )
{
  m_Barriers.push_back( barrier );
}

void Ember::FG::Context::FlushBarriers()
{
  if ( m_Barriers.empty() ) return;

  m_FrameData.CommandList->ResourceBarrier( CountOf( m_Barriers ), DataOf( m_Barriers ) );
  m_Barriers.clear();
}

Ember::FG::Texture Ember::FG::Context::CreateTexture( Texture::Desc const& desc )
{
  uint64_t const hash      = HashFnv1A( sizeof( Texture::Desc ), ( byte* )&desc );

  auto           it        = m_Textures.Find( hash );
  auto&          res_queue = it == m_Textures.end() ? m_Textures.Put( hash, {} ) : it->second;

  if ( not res_queue.empty() )
  {
    Texture tex = res_queue.front();
    res_queue.pop();
    return tex;
  }

  return CreateTextureImpl( desc );
}

void Ember::FG::Context::DestroyTexture( Texture::Desc const& desc, Texture tex )
{
  uint64_t const hash = HashFnv1A( sizeof( Texture::Desc ), ( byte* )&desc );

  auto           it   = m_Textures.Find( hash );
  if ( it == m_Textures.end() ) return;

  it->second.emplace( std::move( tex ) );
}

Ember::FG::CopySrc::operator uint32_t() const
{
  return ( ( uint32_t )Type & 0x3 );
}

Ember::FG::CopySrc Ember::FG::CopySrc::Decode( uint32_t const flag )
{
  return CopySrc{ ( ReadType )( flag & 0x3 ) };
}

Ember::FG::Attachment::operator uint32_t() const
{
  return ( ( uint32_t )Type & 0x3 ) | ( ( ( uint32_t )Index & 0x7 ) << 2 );
}

Ember::FG::Attachment Ember::FG::Attachment::Decode( uint32_t const flag )
{
  return Attachment{ ( WriteType )( flag & 0x3 ), ( uint8_t )( ( flag >> 2 ) & 0x7 ) };
}

Ember::FG::DepthStencil::operator uint32_t() const
{
  return ( uint32_t )Type & 0x3;
}

Ember::FG::DepthStencil Ember::FG::DepthStencil::Decode( uint32_t const flag )
{
  return DepthStencil{ ( WriteType )( flag & 0x3 ) };
}

Ember::FG::CopyDst::operator uint32_t() const
{
  return ( uint32_t )Type & 0x3;
}

Ember::FG::CopyDst Ember::FG::CopyDst::Decode( uint32_t const flag )
{
  return CopyDst{ ( WriteType )( flag & 0x3 ) };
}

Ember::FG::Read Ember::FG::DecodeReadFlags( uint32_t const v )
{
  switch ( ( ReadType )( v & 0x3 ) )
  {
    case ReadType::kDSV:
      UNREACHABLE; // Not supported
    case ReadType::kSRV:
      UNREACHABLE; // Not supported
    case ReadType::kCBV:
      UNREACHABLE; // Not supported
    case ReadType::kCopy:
      return CopySrc::Decode( v );
  }
  UNREACHABLE;
}

Ember::FG::Write Ember::FG::DecodeWriteFlags( uint32_t const v )
{
  switch ( ( WriteType )( v & 0x3 ) )
  {
    case WriteType::kRTV:
      return Attachment::Decode( v );
    case WriteType::kDSV:
      return DepthStencil::Decode( v );
    case WriteType::kCopy:
      return CopyDst::Decode( v );
  }
  UNREACHABLE;
}

// ReSharper disable once CppInconsistentNaming
void Ember::FG::Texture::create( Desc const& desc, void* alloc )
{
  ASSERT( alloc );
  Context* context = ( Context* )alloc;
  *this            = context->CreateTexture( desc );
}

// ReSharper disable once CppInconsistentNaming
void Ember::FG::Texture::destroy( Desc const& desc, void* alloc )
{
  ASSERT( alloc );
  Context* context = ( Context* )alloc;
  context->DestroyTexture( desc, std::move( *this ) );
}

// ReSharper disable once CppInconsistentNaming
void Ember::FG::Texture::preRead( Desc const& desc, uint32_t flags, void* context )
{
  ASSERT( context );
  Context* ctx     = ( Context* )context;

  auto     decoded = DecodeReadFlags( flags );

  switch ( decoded.index() )
  {
    case 3:
    {
      if ( CurrentState != D3D12_RESOURCE_STATE_COPY_SOURCE )
      {
        ctx->PushBarrier(
            CD3DX12_RESOURCE_BARRIER::Transition( Resource.Get(), CurrentState, D3D12_RESOURCE_STATE_COPY_SOURCE ) );
        CurrentState = D3D12_RESOURCE_STATE_COPY_SOURCE;
      }
    }
    break;
  }
}

// ReSharper disable once CppInconsistentNaming
void Ember::FG::Texture::preWrite( Desc const& desc, uint32_t const flags, void* context )
{
  ASSERT( context );
  Context* ctx     = ( Context* )context;

  auto     decoded = DecodeWriteFlags( flags );

  switch ( decoded.index() )
  {
    case 0:
    {
      ASSERT( desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET );

      if ( CurrentState != D3D12_RESOURCE_STATE_RENDER_TARGET )
      {
        ctx->PushBarrier(
            CD3DX12_RESOURCE_BARRIER::Transition( Resource.Get(), CurrentState, D3D12_RESOURCE_STATE_RENDER_TARGET ) );
        CurrentState = D3D12_RESOURCE_STATE_RENDER_TARGET;
      }
    }
    break;
    case 1:
    {
      ASSERT( desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL );

      if ( CurrentState != D3D12_RESOURCE_STATE_DEPTH_WRITE )
      {
        ctx->PushBarrier(
            CD3DX12_RESOURCE_BARRIER::Transition( Resource.Get(), CurrentState, D3D12_RESOURCE_STATE_DEPTH_WRITE ) );
        CurrentState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
      }
    }
    break;
    case 2:
    {
      if ( CurrentState != D3D12_RESOURCE_STATE_COPY_DEST )
      {
        ctx->PushBarrier(
            CD3DX12_RESOURCE_BARRIER::Transition( Resource.Get(), CurrentState, D3D12_RESOURCE_STATE_COPY_DEST ) );
        CurrentState = D3D12_RESOURCE_STATE_COPY_DEST;
      }
    }
    break;
    default:
      UNREACHABLE;
  }
}
