#include "Atmosphere.hpp"

#include <Graphics/RenderDevice.hpp>
#include <Util/DataUtil.hpp>
#include <Util/HelperUtils.hpp>
#include <Util/Profiling.hpp>
#include <format>
#include "FrameGraphHelper.hpp"
#include "fg/Blackboard.hpp"
#include "fg/FrameGraph.hpp"

Ember::RenderPass::Atmosphere::Atmosphere(
    ComPtr<ID3D12RootSignature> root_signature,
    ComPtr<ID3D12PipelineState> transmittance_lut_pipeline,
    ComPtr<ID3D12PipelineState> sky_view_lut_pipeline,
    Texture                     transmittance_lut,
    Texture                     sky_view_lut,
    std::vector<Buffer>         atmosphere_param_buffers )
  : m_RootSignature{ std::move( root_signature ) }
  , m_TransmittanceLUTPipeline{ std::move( transmittance_lut_pipeline ) }
  , m_SkyViewLUTPipeline{ std::move( sky_view_lut_pipeline ) }
  , m_TransmittanceLUT{ std::move( transmittance_lut ) }
  , m_SkyViewLUT{ std::move( sky_view_lut ) }
  , m_AtmosphereParamBuffers{ std::move( atmosphere_param_buffers ) }
  , m_LUTUpdatePendingFrames{ ( uint8_t )m_AtmosphereParamBuffers.size() }
{}

bool Ember::RenderPass::Atmosphere::Create( Atmosphere* out, RenderDevice* render_device )
{
  Texture transmittance_lut = render_device->CreateTexture2D( {
      .Format    = kTransmittanceLUTFormat,
      .Width     = kTransmittanceLUTSize.x,
      .Height    = kTransmittanceLUTSize.y,
      .Usage     = TextureUsage::kRenderTarget,
      .MipLevels = MipLevels::kBase,
      .InitState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
  } );
  transmittance_lut.SetName( L"Transmittance LUT" );

  Texture sky_view_lut = render_device->CreateTexture2D( {
      .Format    = DXGI_FORMAT_R16G16B16A16_FLOAT,
      .Width     = kSkyViewLUTSize.x,
      .Height    = kSkyViewLUTSize.y,
      .Usage     = TextureUsage::kRenderTarget,
      .MipLevels = MipLevels::kBase,
      .InitState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
  } );
  sky_view_lut.SetName( L"Sky View LUT" );

  D3D12_ROOT_PARAMETER1 root_parameters[] = {
    RootConstantBuffer{ .Register = 0 },
    RootConstantBuffer{ .Register = 1 },
    RootConstants{ .Register = 2, .SizeBytes = sizeof( uint32_t ) },
  };

  D3D12_STATIC_SAMPLER_DESC static_sampler_desc = CD3DX12_STATIC_SAMPLER_DESC{ 0 };

  //
  auto root_signature = render_device->CreateRootSignature( {
      .RootParameters = root_parameters,
      .StaticSamplers = { &static_sampler_desc, 1 },
      .ShaderAccess   = RootSignatureDesc::Access::kVertexPixel,
      .DebugName      = "Atmosphere Root Signature",
  } );
  if ( not root_signature ) return false;

  auto transmittance_lut_pipeline = render_device->CreateGraphicsPipeline( {
      .RootSignature    = root_signature.Get(),
      .RTVFormats       = { &kTransmittanceLUTFormat, 1 },
      .VertexShaderName = "ScreenSpaceTriangleVS.cso",
      .PixelShaderName  = "TransmittanceLUTPS.cso",
      .DebugName        = "Transmittance LUT Pipeline",
  } );
  if ( not transmittance_lut_pipeline ) return false;

  auto sky_view_lut_pipeline = render_device->CreateGraphicsPipeline( {
      .RootSignature    = root_signature.Get(),
      .RTVFormats       = { &kSkyViewLUTFormat, 1 },
      .VertexShaderName = "ScreenSpaceTriangleVS.cso",
      .PixelShaderName  = "SkyViewLUTPS.cso",
      .DebugName        = "Sky View LUT Pipeline",
  } );
  if ( not sky_view_lut_pipeline ) return false;

  std::vector<Buffer> atmosphere_param_buffers;
  wchar_t             buf[64];
  for ( uint32_t i = 0; i < RenderDevice::kNumFrames; ++i )
  {
    Buffer& atmosphere_param_buffer =
        atmosphere_param_buffers.emplace_back( render_device->CreateConstantBuffer( sizeof( Params ) + 4 ) );
    auto const res = std::format_to_n( buf, CountOf( buf ), L"Atmosphere Parameter CB {}", i );
    ASSERT( res.size < CountOf( buf ) );
    atmosphere_param_buffer.SetName( buf );
  }

  new ( out ) Atmosphere{
    std::move( root_signature ),        std::move( transmittance_lut_pipeline ),
    std::move( sky_view_lut_pipeline ), std::move( transmittance_lut ),
    std::move( sky_view_lut ),          std::move( atmosphere_param_buffers ),
  };

  return true;
}

Ember::RenderPass::Atmosphere::Data Ember::RenderPass::Atmosphere::Execute(
    FrameGraph* frame_graph, FrameGraphBlackboard* blackboard, uint32_t const frame_idx )
{
  if ( m_LUTUpdatePendingFrames ) m_AtmosphereParamBuffers[frame_idx].Write( 0, sizeof( Params ), &m_AtmosphereParams );
  m_AtmosphereParamBuffers[frame_idx].Write( sizeof( Params ), 4, &m_Sun );

  // TODO: Resource layout breaks when sky_view is not rendered, but transmittance is updated.
  // Need to fix by supporting persistent resources in frame graph allocator.
  FrameGraphResource const transmittance_lut = frame_graph->import(
      "Transmittance LUT",
      {
          .Format    = kTransmittanceLUTFormat,
          .Width     = kTransmittanceLUTSize.x,
          .Height    = kTransmittanceLUTSize.y,
          .MipLevels = MipLevels::kBase,
          .Usage     = TextureUsage::kRenderTarget,
          .InitState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
      },
      FG::Texture{ m_TransmittanceLUT } );

  FrameGraphResource const param_buffer =
      frame_graph->import( "Param Buffer", FG::Buffer::Desc{}, FG::Buffer{ m_AtmosphereParamBuffers[frame_idx] } );

  bool const transmittance_lut_needs_update = m_LUTUpdatePendingFrames;
  if ( m_LUTUpdatePendingFrames ) --m_LUTUpdatePendingFrames;

  Data const transmittance = frame_graph->addCallbackPass(
      "Update Transmittance LUT",
      [&]( FrameGraph::Builder& builder, Data& data )
      {
        if ( transmittance_lut_needs_update )
        {
          data.TransmittanceLUT = builder.write(
              transmittance_lut,
              FG::Attachment{
                  .Index  = 0,
                  .LoadOp = FG::LoadOperation::kDiscard,
              } );
        }
        else
        {
          data.TransmittanceLUT = transmittance_lut;
        }
        data.AtmosphereParams = builder.read( param_buffer );
      },
      [self = this]( Data const& data, FrameGraphPassResources& resources, FG::Context const* context )
      {
        ZoneScopedN( "Update Transmittance LUT" );

        FG::Context::FrameData const& frame_data = context->GetFrameData();
        CommandList*                  cmd        = frame_data.CommandList;
        PIXScopedEvent( cmd->Get(), PIX_COLOR_DEFAULT, "Update Transmittance LUT" );

        auto const& [params_buf] = resources.get<FG::Buffer>( data.AtmosphereParams );

        cmd->SetGraphicsRootSignature( self->m_RootSignature.Get() );
        cmd->SetPipelineState( self->m_TransmittanceLUTPipeline.Get() );
        cmd->SetGraphicsRootConstantBuffer( 0, params_buf );
        cmd->DrawInstanced( 3, 1, 0, 0 );
      } );

  Data const sky = frame_graph->addCallbackPass(
      "Update Sky View LUT",
      [&]( FrameGraph::Builder& builder, Data& data )
      {
        FrameGraphResource const sky_view_lut = builder.create<FG::Texture>(
            "Sky View LUT",
            {
                .Format    = kSkyViewLUTFormat,
                .Width     = kSkyViewLUTSize.x,
                .Height    = kSkyViewLUTSize.y,
                .MipLevels = MipLevels::kBase,
                .Usage     = TextureUsage::kRenderTarget,
                .InitState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            } );
        data.TransmittanceLUT = builder.read( transmittance.TransmittanceLUT, FG::ShaderRead{} );

        data.AtmosphereParams = builder.read( param_buffer );
        data.SkyViewLUT       = builder.write(
            sky_view_lut,
            FG::Attachment{
                      .Index  = 0,
                      .LoadOp = FG::LoadOperation::kDiscard,
            } );
      },
      [self = this, blackboard]( Data const& data, FrameGraphPassResources& resources, FG::Context const* context )
      {
        ZoneScopedN( "Update Sky View LUT" );

        FG::Context::FrameData const& frame_data = context->GetFrameData();
        CommandList*                  cmd        = frame_data.CommandList;
        PIXScopedEvent( cmd->Get(), PIX_COLOR_DEFAULT, "Update Sky View LUT" );

        FG::Texture const& transmittance_lut = resources.get<FG::Texture>( data.TransmittanceLUT );
        auto const& [params_buf]             = resources.get<FG::Buffer>( data.AtmosphereParams );

        auto const& [constants_buf]          = blackboard->get<FrameConstants>();

        cmd->IASetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
        cmd->SetGraphicsRootSignature( self->m_RootSignature.Get() );
        cmd->SetPipelineState( self->m_SkyViewLUTPipeline.Get() );
        cmd->SetGraphicsRootConstantBuffer( 0, params_buf );
        cmd->SetGraphicsRootConstantBuffer( 1, constants_buf );
        cmd->SetGraphicsRootConstants( 2, transmittance_lut.GetSRVHandle() );
        cmd->DrawInstanced( 3, 1, 0, 0 );
      } );

  return sky;
}

Ember::RenderPass::Atmosphere::Data Ember::RenderPass::Atmosphere::operator()(
    FrameGraph* frame_graph, FrameGraphBlackboard* blackboard, uint32_t const frame_idx )
{
  return Execute( frame_graph, blackboard, frame_idx );
}

void Ember::RenderPass::Atmosphere::ResetSun()
{
  SetSun( 0xFFFFFFFF );
}

void Ember::RenderPass::Atmosphere::SetSun( uint32_t const sun_index )
{
  m_Sun = sun_index;
}

void Ember::RenderPass::Atmosphere::SetAtmosphereParams( Params const& atmosphere_params )
{
  m_AtmosphereParams       = atmosphere_params;
  m_LUTUpdatePendingFrames = true;
}

Ember::RenderPass::Atmosphere::Params const& Ember::RenderPass::Atmosphere::GetAtmosphereParams() const
{
  return m_AtmosphereParams;
}
