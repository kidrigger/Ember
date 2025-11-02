#include "FrameGraphHelper.hpp"

#include <numeric>

#include "RenderDevice.hpp"
#include "RenderTargetManager.hpp"
#include "Util/DataUtil.hpp"
#include "Util/HelperUtils.hpp"

#pragma comment( lib, "FrameGraph.lib" )

namespace
{
class ShiftingEncoder
{
  int      m_CurrentShift{ 0 };
  uint32_t m_Result{ 0 };

public:
  ShiftingEncoder() = default;

  ShiftingEncoder& Push( auto const& value, int const bits )
    requires requires { static_cast<uint32_t>( value ); }
  {
    ASSERT( m_CurrentShift + bits <= 32 );
    m_Result       |= ( ( ( uint32_t )value & ( ( 1 << bits ) - 1 ) ) << m_CurrentShift );
    m_CurrentShift += bits;
    return *this;
  }

  operator uint32_t() const
  {
    return m_Result;
  }
};

class ShiftingDecoder
{
  uint32_t m_Value;
  int      m_CurrentShift{ 0 };

public:
  explicit ShiftingDecoder( uint32_t const value ) : m_Value{ value }
  {}

  template <typename T>
  auto Pop( int const bits )
    requires requires { static_cast<T>( ( uint32_t )0 ); }
  {
    ASSERT( sizeof( T ) * 8 >= bits );
    ASSERT( ( 32 - m_CurrentShift ) >= bits );

    int const shift  = m_CurrentShift;
    m_CurrentShift  += bits;

    return ( T )( ( m_Value >> shift ) & ( ( 1 << bits ) - 1 ) );
  }
};
} // namespace

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
  : m_RenderDevice{ render_device }
  , m_RenderTargetManager{ std::move( render_target_manager ) }
  , m_FrameData{}
  , m_CurrentDepthTarget{}
  , m_RenderTargetSize{}
  , m_TickCounter{ 0 }
  , m_TextureCount{ 0 }
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

void Ember::FG::Context::SetRenderTarget(
    uint32_t const       index,
    Texture const&       render_target,
    Texture::Desc const& desc,
    bool const           as_srgb,
    LoadOperation const  load_op )
{
  if ( m_RenderTargetSize.x == 0 ) m_RenderTargetSize = { desc.Width, desc.Height };

  ASSERT_M(
      m_RenderTargetSize.x == desc.Width and m_RenderTargetSize.y == desc.Height, "All RTs must have the same size." );

  if ( m_CurrentRenderTargets.Resources.size() <= index )
  {
    m_CurrentRenderTargets.Resources.resize( index + 1 );
    m_CurrentRenderTargets.Descriptions.resize( index + 1 );
    m_CurrentRenderTargets.LoadOps.resize( index + 1 );
  }

  m_CurrentRenderTargets.Resources[index] = render_target.Resource.Get();
  m_CurrentRenderTargets.Descriptions[index] = {
    .Format        = as_srgb ? DirectX::MakeSRGB(desc.Format) : desc.Format,
    .ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D,
    .Texture2D     = {
      .MipSlice = 0,
      .PlaneSlice = 0,
    },
  };
  m_CurrentRenderTargets.LoadOps[index] = load_op;
}

void Ember::FG::Context::SetDepthTarget(
    Texture const& depth_target, Texture::Desc const& desc, LoadOperation const load_op )
{
  if ( m_RenderTargetSize.x == 0 ) m_RenderTargetSize = { desc.Width, desc.Height };

  ASSERT_M(
      m_RenderTargetSize.x == desc.Width and m_RenderTargetSize.y == desc.Height,
      "Depth Target must have the same size as Render Targets." );

  m_CurrentDepthTarget = {
    .Resource = depth_target.Resource.Get(),
    .Desc     = {
      .Format        = desc.Format,
      .ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D,
      .Flags         = D3D12_DSV_FLAG_NONE,
      .Texture2D     = { .MipSlice = 0 },
    },
    .LoadOp = load_op,
  };
}

void Ember::FG::Context::PreparePass()
{
  FlushBarriers();

  uint32_t const rt_count = CountOf( m_CurrentRenderTargets.Resources );

  ASSERT_M(
      std::accumulate(
          m_CurrentRenderTargets.Resources.begin(),
          m_CurrentRenderTargets.Resources.end(),
          true,
          []( bool const acc, ID3D12Resource const* res ) { return acc and res != nullptr; } ),
      "All Render Targets from 0 to Largest must be set." );

  m_RenderTargetManager->OMSetRenderTargets(
      m_FrameData.CommandList,
      rt_count,
      DataOf( m_CurrentRenderTargets.Resources ),
      DataOf( m_CurrentRenderTargets.Descriptions ),
      m_CurrentDepthTarget.Resource,
      &m_CurrentDepthTarget.Desc );

  m_RenderTargetManager->RSSetScissorViewport( m_FrameData.CommandList, m_RenderTargetSize.x, m_RenderTargetSize.y );

  for ( uint32_t i = 0; i < rt_count; i++ )
  {
    switch ( m_CurrentRenderTargets.LoadOps[i] )
    {
      case LoadOperation::kLoad:
        break;
      case LoadOperation::kDiscard:
      {
        m_FrameData.CommandList->DiscardResource( m_CurrentRenderTargets.Resources[i], nullptr );
      }
      break;
      case LoadOperation::kClear:
      {
        FLOAT constexpr kBlack[4] = {};
        m_RenderTargetManager->ClearRenderTargetView(
            m_FrameData.CommandList, m_CurrentRenderTargets.Resources[i], kBlack );
      }
      break;
    }
  }

  switch ( m_CurrentDepthTarget.LoadOp )
  {
    case LoadOperation::kLoad:
      break;
    case LoadOperation::kDiscard:
    {
      m_FrameData.CommandList->DiscardResource( m_CurrentDepthTarget.Resource, nullptr );
    }
    break;
    case LoadOperation::kClear:
    {
      m_RenderTargetManager->ClearDepthStencilView(
          m_FrameData.CommandList, m_CurrentDepthTarget.Resource, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0 );
    }
    break;
  }

  // Clear for next pass
  m_CurrentDepthTarget.Resource = nullptr;
  m_CurrentRenderTargets.Resources.clear();
  m_CurrentRenderTargets.Descriptions.clear();
  m_RenderTargetSize = {};
}

Ember::FG::Texture Ember::FG::Context::CreateTexture( Texture::Desc const& desc )
{
  uint64_t const hash      = HashFnv1A( sizeof( Texture::Desc ), ( byte* )&desc );

  auto           it        = m_TransientTextures.Find( hash );
  auto&          res_queue = it == m_TransientTextures.end() ? m_TransientTextures.Put( hash, {} ) : it->second;

  res_queue.TickStamp      = m_TickCounter;

  if ( not res_queue.Empty() )
  {
    return res_queue.Pop();
  }

  m_TextureCount++;
  return CreateTextureImpl( desc );
}

void Ember::FG::Context::DestroyTexture( Texture::Desc const& desc, Texture tex )
{
  uint64_t const hash = HashFnv1A( sizeof( Texture::Desc ), ( byte* )&desc );

  auto           it   = m_TransientTextures.Find( hash );
  if ( it == m_TransientTextures.end() ) return;

  auto& srv_cache = m_TextureSRVCache[tex.Resource.Get()];
  for ( SRVHandle const& handle : srv_cache.Values() )
  {
    m_RenderDevice->FreeHandle( handle );
  }
  srv_cache.Clear();

  tex.AsSRV = {};

  it->second.Push( std::move( tex ) );
}

uint32_t Ember::FG::Context::GetTextureCount() const
{
  return m_TextureCount;
}

void Ember::FG::Context::Update()
{
  m_TickCounter++;

  m_TransientTextures.EraseIf(
      [&]( uint64_t const&, TexturePoolEntry const& val )
      {
        bool const marked_del = val.Empty() or m_TickCounter - val.TickStamp >= kMaxAge;
        if ( marked_del ) m_TextureCount -= ( uint32_t )val.Queue.size();
        return marked_del;
      } );
}

Ember::SRVHandle Ember::FG::Context::GetOrCreateSRVHandle(
    Texture const& texture, CD3DX12_SHADER_RESOURCE_VIEW_DESC const& srv_desc )
{
  auto&          cache = m_TextureSRVCache[texture.Resource.Get()];

  uint64_t const hash  = HashFnv1A( srv_desc );
  if ( auto it = cache.Find( hash ); it != cache.end() )
  {
    return it->second;
  }

  SRVHandle const handle = m_RenderDevice->CreateBindlessHandle( texture.Resource.Get(), srv_desc );
  return cache.Put( hash, handle );
}

Ember::FG::Context::~Context()
{
  for ( auto const& cache : m_TextureSRVCache.Values() )
  {
    for ( auto const& handle : cache.Values() )
    {
      m_RenderDevice->FreeHandle( handle );
    }
  }
}

Ember::FG::DepthStencilRead::operator uint32_t() const
{
  return ShiftingEncoder{}.Push( Type, 2 );
}

Ember::FG::DepthStencilRead Ember::FG::DepthStencilRead::Decode( uint32_t const flag )
{
  ShiftingDecoder decoder{ flag };
  return DepthStencilRead{
    .Type = decoder.Pop<ReadType>( 2 ),
  };
}

Ember::FG::ShaderResource::operator uint32_t() const
{
  return ShiftingEncoder{}.Push( Type, 2 ).Push( PixelShaderUse, 1 ).Push( OnlyTopMip, 1 );
}

Ember::FG::ShaderResource Ember::FG::ShaderResource::Decode( uint32_t const flag )
{
  ShiftingDecoder decoder{ flag };
  return ShaderResource{
    .Type           = decoder.Pop<ReadType>( 2 ),
    .PixelShaderUse = decoder.Pop<bool>( 1 ),
    .OnlyTopMip     = decoder.Pop<bool>( 1 ),
  };
}

Ember::FG::CopySrc::operator uint32_t() const
{
  return ShiftingEncoder{}.Push( Type, 2 );
}

Ember::FG::CopySrc Ember::FG::CopySrc::Decode( uint32_t const flag )
{
  ShiftingDecoder decoder{ flag };
  return CopySrc{
    .Type = decoder.Pop<ReadType>( 2 ),
  };
}

Ember::FG::Attachment::operator uint32_t() const
{
  return ShiftingEncoder{}.Push( Type, 2 ).Push( Index, 3 ).Push( ForceSrgb, 1 ).Push( LoadOp, 2 );
}

Ember::FG::Attachment Ember::FG::Attachment::Decode( uint32_t const flag )
{
  ShiftingDecoder decoder{ flag };
  return Attachment{
    .Type      = decoder.Pop<WriteType>( 2 ),
    .Index     = decoder.Pop<uint8_t>( 3 ),
    .ForceSrgb = decoder.Pop<bool>( 1 ),
    .LoadOp    = decoder.Pop<LoadOperation>( 2 ),
  };
}

Ember::FG::DepthStencil::operator uint32_t() const
{
  return ShiftingEncoder{}.Push( Type, 2 ).Push( LoadOp, 2 );
}

Ember::FG::DepthStencil Ember::FG::DepthStencil::Decode( uint32_t const flag )
{
  ShiftingDecoder decoder{ flag };
  return DepthStencil{
    .Type   = decoder.Pop<WriteType>( 2 ),
    .LoadOp = decoder.Pop<LoadOperation>( 2 ),
  };
}

Ember::FG::CopyDst::operator uint32_t() const
{
  return ShiftingEncoder{}.Push( Type, 2 );
}

Ember::FG::CopyDst Ember::FG::CopyDst::Decode( uint32_t const flag )
{
  ShiftingDecoder decoder{ flag };
  return CopyDst{ .Type = decoder.Pop<WriteType>( 2 ) };
}

Ember::FG::Read Ember::FG::DecodeReadFlags( uint32_t const v )
{
  switch ( ( ReadType )( v & 0x3 ) )
  {
    case ReadType::kDSV:
      return DepthStencilRead::Decode( v );
    case ReadType::kSRV:
      return ShaderResource::Decode( v );
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
void Ember::FG::Texture::preRead( Desc const& desc, uint32_t const flags, void* context )
{
  ASSERT( context );
  Context*   ctx     = ( Context* )context;

  auto const decoded = DecodeReadFlags( flags );

  switch ( ( ReadType )decoded.index() )
  {
    case ReadType::kDSV:
    {
      ASSERT( desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL );
      auto const depth_stencil = std::get<DepthStencilRead>( decoded );

      if ( CurrentState != D3D12_RESOURCE_STATE_DEPTH_READ )
      {
        ctx->PushBarrier(
            CD3DX12_RESOURCE_BARRIER::Transition( Resource.Get(), CurrentState, D3D12_RESOURCE_STATE_DEPTH_READ ) );
        CurrentState = D3D12_RESOURCE_STATE_DEPTH_READ;
      }
      ctx->SetDepthTarget( *this, desc, LoadOperation::kLoad );
    }
    break;
    case ReadType::kSRV:
    {
      auto const srv            = std::get<ShaderResource>( decoded );
      auto const required_state = srv.PixelShaderUse ? D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
                                                     : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

      auto       srv_desc       = CD3DX12_SHADER_RESOURCE_VIEW_DESC::Tex2D( desc.Format, srv.OnlyTopMip ? 1 : -1 );

      AsSRV                     = ctx->GetOrCreateSRVHandle( *this, srv_desc );

      if ( CurrentState != required_state )
      {
        ctx->PushBarrier( CD3DX12_RESOURCE_BARRIER::Transition( Resource.Get(), CurrentState, required_state ) );
        CurrentState = required_state;
      }
    }
    break;
    case ReadType::kCopy:
    {
      if ( CurrentState != D3D12_RESOURCE_STATE_COPY_SOURCE )
      {
        ctx->PushBarrier(
            CD3DX12_RESOURCE_BARRIER::Transition( Resource.Get(), CurrentState, D3D12_RESOURCE_STATE_COPY_SOURCE ) );
        CurrentState = D3D12_RESOURCE_STATE_COPY_SOURCE;
      }
    }
    break;
    default:
      UNREACHABLE;
  }
}

// ReSharper disable once CppInconsistentNaming
void Ember::FG::Texture::preWrite( [[maybe_unused]] Desc const& desc, uint32_t const flags, void* context )
{
  ASSERT( context );
  Context* ctx     = ( Context* )context;

  auto     decoded = DecodeWriteFlags( flags );

  switch ( ( WriteType )decoded.index() )
  {
    case WriteType::kRTV:
    {
      ASSERT( desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET );
      auto const attachment = std::get<Attachment>( decoded );

      if ( CurrentState != D3D12_RESOURCE_STATE_RENDER_TARGET )
      {
        ctx->PushBarrier(
            CD3DX12_RESOURCE_BARRIER::Transition( Resource.Get(), CurrentState, D3D12_RESOURCE_STATE_RENDER_TARGET ) );
        CurrentState = D3D12_RESOURCE_STATE_RENDER_TARGET;
      }
      ctx->SetRenderTarget( attachment.Index, *this, desc, attachment.ForceSrgb, attachment.LoadOp );
    }
    break;
    case WriteType::kDSV:
    {
      ASSERT( desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL );
      auto const depth_stencil = std::get<DepthStencil>( decoded );

      if ( CurrentState != D3D12_RESOURCE_STATE_DEPTH_WRITE )
      {
        ctx->PushBarrier(
            CD3DX12_RESOURCE_BARRIER::Transition( Resource.Get(), CurrentState, D3D12_RESOURCE_STATE_DEPTH_WRITE ) );
        CurrentState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
      }
      ctx->SetDepthTarget( *this, desc, depth_stencil.LoadOp );
    }
    break;
    case WriteType::kCopy:
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

bool Ember::FG::Context::TexturePoolEntry::Empty() const
{
  return Queue.empty();
}

Ember::FG::Texture Ember::FG::Context::TexturePoolEntry::Pop()
{
  Texture tex = Queue.front();
  Queue.pop();

  return tex;
}

void Ember::FG::Context::TexturePoolEntry::Push( Texture tex )
{
  Queue.push( std::move( tex ) );
}
