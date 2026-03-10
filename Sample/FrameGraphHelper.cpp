#include "FrameGraphHelper.hpp"

#include <numeric>

#include <Graphics/RenderDevice.hpp>
#include <Util/DataUtil.hpp>
#include <Util/DirectXHeaders.hpp>
#include <Util/HelperUtils.hpp>

#include "TexturePool.hpp"

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

Ember::FG::Context::Context( RenderDevice* render_device )
  : m_RenderDevice{ render_device }, m_FrameData{}, m_CurrentDepthTarget{}, m_RenderTargetSize{}
{}

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

void Ember::FG::Context::PushBarrier( CD3DX12_RESOURCE_BARRIER const& barrier )
{
  m_Barriers.push_back( barrier );
}

void Ember::FG::Context::FlushBarriers()
{
  if ( m_Barriers.empty() ) return;

  m_FrameData.CommandList->ResourceBarrier( m_Barriers );
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

  m_CurrentRenderTargets.Resources[index] = render_target.GetTexture();
  if ( render_target.GetDesc().Dim == TextureDim::kCube )
  {
    m_CurrentRenderTargets.Descriptions[index] = {
      .Format         = as_srgb ? DirectX::MakeSRGB( desc.Format ) : desc.Format,
      .ViewDimension  = D3D12_RTV_DIMENSION_TEXTURE2DARRAY,
      .Texture2DArray = { .MipSlice = 0, .FirstArraySlice = 0, .ArraySize = 6, .PlaneSlice = 0, },
    };
  }
  else
  {
    // Texture2D
    m_CurrentRenderTargets.Descriptions[index] = {
      .Format        = as_srgb ? DirectX::MakeSRGB( desc.Format ) : desc.Format,
      .ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D,
      .Texture2D     = {
        .MipSlice   = 0,
        .PlaneSlice = 0,
      },
    };
  }

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
    .Resource = depth_target.GetTexture(),
    .Desc     = {
      .Format = desc.Format,
      .Flags  = D3D12_DSV_FLAG_NONE,
    },
    .LoadOp = load_op,
  };

  switch ( desc.Dim )
  {
    case TextureDim::k2D:
    {
      m_CurrentDepthTarget.Desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
      m_CurrentDepthTarget.Desc.Texture2D     = { .MipSlice = 0 };
    }
    break;
    case TextureDim::kCube:
    {
      m_CurrentDepthTarget.Desc.ViewDimension  = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
      m_CurrentDepthTarget.Desc.Texture2DArray = {
        .MipSlice        = 0,
        .FirstArraySlice = 0,
        .ArraySize       = desc.ArraySize * 6u,
      };
    }
    break;
    default:
      UNREACHABLE;
  }
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

  m_FrameData.CommandList->RSSetScissorViewport( m_RenderTargetSize.x, m_RenderTargetSize.y );

  for ( uint32_t i = 0; i < rt_count; i++ )
  {
    switch ( m_CurrentRenderTargets.LoadOps[i] )
    {
      case LoadOperation::kLoad:
        break;
      case LoadOperation::kDiscard:
      {
        m_FrameData.CommandList->DiscardResource( m_CurrentRenderTargets.Resources[i] );
      }
      break;
      case LoadOperation::kClear:
      {
        FLOAT constexpr kBlack[4] = {};
        m_FrameData.CommandList->ClearRenderTargetView( m_CurrentRenderTargets.Resources[i], kBlack );
      }
      break;
      default:
        UNREACHABLE;
    }
  }

  if ( m_CurrentDepthTarget.Resource )
  {
    switch ( m_CurrentDepthTarget.LoadOp )
    {
      case LoadOperation::kLoad:
        break;
      case LoadOperation::kDiscard:
      {
        m_FrameData.CommandList->DiscardResource( m_CurrentDepthTarget.Resource );
      }
      break;
      case LoadOperation::kClear:
      {
        m_FrameData.CommandList->ClearDepthStencilView(
            m_CurrentDepthTarget.Resource, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0 );
      }
      break;
      default:
        UNREACHABLE;
    }
  }

  m_FrameData.CommandList->OMSetRenderTargets(
      rt_count,
      DataOf( m_CurrentRenderTargets.Resources ),
      DataOf( m_CurrentRenderTargets.Descriptions ),
      m_CurrentDepthTarget.Resource,
      m_CurrentDepthTarget.Resource ? &m_CurrentDepthTarget.Desc : nullptr );

  // Clear for next pass
  m_CurrentDepthTarget.Resource = nullptr;
  m_CurrentDepthTarget.Desc     = {};
  m_CurrentRenderTargets.Resources.clear();
  m_CurrentRenderTargets.Descriptions.clear();
  m_RenderTargetSize = {};
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

Ember::FG::ShaderRead::operator uint32_t() const
{
  return ShiftingEncoder{}.Push( Type, 2 ).Push( PixelShaderUse, 1 );
}

Ember::FG::ShaderRead Ember::FG::ShaderRead::Decode( uint32_t const flag )
{
  ShiftingDecoder decoder{ flag };
  return ShaderRead{
    .Type           = decoder.Pop<ReadType>( 2 ),
    .PixelShaderUse = decoder.Pop<bool>( 1 ),
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

Ember::FG::ShaderWrite::operator uint32_t() const
{
  return ShiftingEncoder{}.Push( Type, 2 );
}

Ember::FG::ShaderWrite Ember::FG::ShaderWrite::Decode( uint32_t const flag )
{
  ShiftingDecoder decoder{ flag };
  return ShaderWrite{
    .Type = decoder.Pop<WriteType>( 2 ),
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
      return ShaderRead::Decode( v );
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
    case WriteType::kUAV:
      return ShaderWrite::Decode( v );
    case WriteType::kCopy:
      return CopyDst::Decode( v );
  }
  UNREACHABLE;
}

Ember::FG::Texture::Texture( Super const& other ) : Super( other )
{}

Ember::FG::Texture::Texture( Super&& other ) noexcept : Super( std::forward<Super>( other ) )
{}

Ember::FG::Texture& Ember::FG::Texture::operator=( Super const& other )
{
  if ( this != &other )
  {
    Super::operator=( other );
  }
  return *this;
}

Ember::FG::Texture& Ember::FG::Texture::operator=( Super&& other ) noexcept
{
  if ( this != &other )
  {
    Super::operator=( std::forward<Super>( other ) );
  }
  return *this;
}

// ReSharper disable once CppInconsistentNaming
void Ember::FG::Texture::create( Desc const& desc, void* alloc )
{
  ASSERT( alloc );
  TexturePool* allocator = ( TexturePool* )alloc;
  *this                  = allocator->CreateTexture( desc );
}

// ReSharper disable once CppInconsistentNaming
void Ember::FG::Texture::destroy( Desc const& desc, void* alloc )
{
  ASSERT( alloc );
  TexturePool* allocator = ( TexturePool* )alloc;
  allocator->DestroyTexture( desc, std::move( *this ) );
}

// ReSharper disable once CppInconsistentNaming
void Ember::FG::Texture::preRead( Desc const& desc, uint32_t const flags, void* context )
{
  ASSERT( context );
  Context*   ctx           = ( Context* )context;

  auto const decoded       = DecodeReadFlags( flags );
  auto const current_state = GetCurrentState();

  switch ( ( ReadType )decoded.index() )
  {
    case ReadType::kDSV:
    {
      ASSERT( desc.Type == TextureType::kDepthStencil );
      [[maybe_unused]] auto const depth_stencil = std::get<DepthStencilRead>( decoded );

      if ( current_state != D3D12_RESOURCE_STATE_DEPTH_READ )
      {
        ctx->PushBarrier(
            CD3DX12_RESOURCE_BARRIER::Transition( GetTexture(), current_state, D3D12_RESOURCE_STATE_DEPTH_READ ) );
        SetCurrentState( D3D12_RESOURCE_STATE_DEPTH_READ );
      }
      ctx->SetDepthTarget( *this, desc, LoadOperation::kLoad );
    }
    break;
    case ReadType::kSRV:
    {
      auto const srv            = std::get<ShaderRead>( decoded );
      auto const required_state = srv.PixelShaderUse ? D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
                                                     : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

      if ( current_state != required_state )
      {
        ctx->PushBarrier( CD3DX12_RESOURCE_BARRIER::Transition( GetTexture(), current_state, required_state ) );
        SetCurrentState( required_state );
      }
    }
    break;
    case ReadType::kCopy:
    {
      if ( current_state != D3D12_RESOURCE_STATE_COPY_SOURCE )
      {
        ctx->PushBarrier(
            CD3DX12_RESOURCE_BARRIER::Transition( GetTexture(), current_state, D3D12_RESOURCE_STATE_COPY_SOURCE ) );
        SetCurrentState( D3D12_RESOURCE_STATE_COPY_SOURCE );
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
  Context*   ctx           = ( Context* )context;

  auto const decoded       = DecodeWriteFlags( flags );
  auto const current_state = GetCurrentState();

  switch ( ( WriteType )decoded.index() )
  {
    case WriteType::kRTV:
    {
      ASSERT( desc.Type == TextureType::kRenderTarget );
      auto const attachment = std::get<Attachment>( decoded );

      if ( current_state != D3D12_RESOURCE_STATE_RENDER_TARGET )
      {
        ctx->PushBarrier(
            CD3DX12_RESOURCE_BARRIER::Transition( GetTexture(), current_state, D3D12_RESOURCE_STATE_RENDER_TARGET ) );
        SetCurrentState( D3D12_RESOURCE_STATE_RENDER_TARGET );
      }
      ctx->SetRenderTarget( attachment.Index, *this, desc, attachment.ForceSrgb, attachment.LoadOp );
    }
    break;
    case WriteType::kDSV:
    {
      ASSERT( desc.Type == TextureType::kDepthStencil );
      auto const depth_stencil = std::get<DepthStencil>( decoded );

      if ( current_state != D3D12_RESOURCE_STATE_DEPTH_WRITE )
      {
        ctx->PushBarrier(
            CD3DX12_RESOURCE_BARRIER::Transition( GetTexture(), current_state, D3D12_RESOURCE_STATE_DEPTH_WRITE ) );
        SetCurrentState( D3D12_RESOURCE_STATE_DEPTH_WRITE );
      }
      ctx->SetDepthTarget( *this, desc, depth_stencil.LoadOp );
    }
    break;
    case WriteType::kUAV:
    {
      if ( current_state != D3D12_RESOURCE_STATE_UNORDERED_ACCESS )
      {
        ctx->PushBarrier( CD3DX12_RESOURCE_BARRIER::Transition(
            GetTexture(), current_state, D3D12_RESOURCE_STATE_UNORDERED_ACCESS ) );
        SetCurrentState( D3D12_RESOURCE_STATE_UNORDERED_ACCESS );
      }
    }
    break;
    case WriteType::kCopy:
    {
      if ( current_state != D3D12_RESOURCE_STATE_COPY_DEST )
      {
        ctx->PushBarrier(
            CD3DX12_RESOURCE_BARRIER::Transition( GetTexture(), current_state, D3D12_RESOURCE_STATE_COPY_DEST ) );
        SetCurrentState( D3D12_RESOURCE_STATE_COPY_DEST );
      }
    }
    break;
    default:
      UNREACHABLE;
  }
}
