#include "AtmosphereContext.hpp"

#include "FrameGraphHelper.hpp"
#include "RenderDevice.hpp"
#include "Util/DataUtil.hpp"
#include "Util/HelperUtils.hpp"
#include "Util/Profiling.hpp"
#include "fg/Blackboard.hpp"
#include "fg/FrameGraph.hpp"

Ember::AtmosphereContext::AtmosphereContext(
    ComPtr<ID3D12RootSignature> root_signature,
    ComPtr<ID3D12PipelineState> transmittance_lut_pipeline,
    ComPtr<ID3D12PipelineState> sky_view_lut_pipeline,
    ComPtr<ID3D12RootSignature> aerial_perspective_root_signature,
    ComPtr<ID3D12PipelineState> aerial_perspective_pipeline,
    Texture                     transmittance_lut,
    Texture                     sky_view_lut,
    std::vector<Buffer>         atmosphere_param_buffers )
  : m_RootSignature{ std::move( root_signature ) }
  , m_TransmittanceLUTPipeline{ std::move( transmittance_lut_pipeline ) }
  , m_SkyViewLUTPipeline{ std::move( sky_view_lut_pipeline ) }
  , m_AerialPerspectiveRootSignature{ std::move( aerial_perspective_root_signature ) }
  , m_AerialPerspectivePipeline{ std::move( aerial_perspective_pipeline ) }
  , m_TransmittanceLUT{ std::move( transmittance_lut ) }
  , m_SkyViewLUT{ std::move( sky_view_lut ) }
  , m_AtmosphereParamBuffers{ std::move( atmosphere_param_buffers ) }
  , m_LUTUpdatePendingFrames{ ( uint8_t )m_AtmosphereParamBuffers.size() }
{}

bool Ember::AtmosphereContext::Create( AtmosphereContext* out, RenderDevice* render_device )
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

  ComPtr<ID3DBlob> vertex_shader_blob;
  ERR_FAIL_RET_F( D3DReadFileToBlob( L"ScreenSpaceTriangleVS.cso", &vertex_shader_blob ) );
  ComPtr<ID3DBlob> transmittance_pixel_shader_blob;
  ERR_FAIL_RET_F( D3DReadFileToBlob( L"TransmittanceLUTPS.cso", &transmittance_pixel_shader_blob ) );
  ComPtr<ID3DBlob> sky_view_pixel_shader_blob;
  ERR_FAIL_RET_F( D3DReadFileToBlob( L"SkyViewLUTPS.cso", &sky_view_pixel_shader_blob ) );
  ComPtr<ID3DBlob> aerial_perspective_comp_shader_blob;
  ERR_FAIL_RET_F( D3DReadFileToBlob( L"AerialPerspectiveCS.cso", &aerial_perspective_comp_shader_blob ) );

  ComPtr<ID3D12Device2>       device                 = render_device->GetDevice();

  D3D_ROOT_SIGNATURE_VERSION  root_signature_version = render_device->FetchHighestRootSignatureVersion();

  ComPtr<ID3D12RootSignature> root_signature;
  ComPtr<ID3D12PipelineState> transmittance_lut_pipeline;
  ComPtr<ID3D12PipelineState> sky_view_lut_pipeline;

  ComPtr<ID3D12RootSignature> aerial_perspective_root_signature;
  ComPtr<ID3D12PipelineState> aerial_perspective_pipeline;

  {
    CD3DX12_ROOT_PARAMETER1 root_parameters[3];
    root_parameters[0].InitAsConstantBufferView( 0 );
    root_parameters[1].InitAsConstants( sizeof( PerFrameConstants ) / 4, 1 );
    root_parameters[2].InitAsConstants( 1, 2 );

    CD3DX12_STATIC_SAMPLER_DESC      static_sampler_desc = CD3DX12_STATIC_SAMPLER_DESC{ 0 };

    D3D12_ROOT_SIGNATURE_FLAGS const root_signature_flags =
        D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED |
        D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_AMPLIFICATION_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_MESH_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS;

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC root_signature_desc;
    root_signature_desc.Init_1_1(
        CountOf( root_parameters ), DataOf( root_parameters ), 1, &static_sampler_desc, root_signature_flags );

    ComPtr<ID3DBlob> root_signature_blob;
    ComPtr<ID3DBlob> error_blob;
    ERR_FAIL_RET_F( D3DX12SerializeVersionedRootSignature(
        &root_signature_desc, root_signature_version, root_signature_blob.ReleaseAndGetAddressOf(), &error_blob ) );

    ERR_FAIL_RET_F( device->CreateRootSignature(
        0,
        root_signature_blob->GetBufferPointer(),
        root_signature_blob->GetBufferSize(),
        IID_PPV_ARGS( &root_signature ) ) );

    D3D12_RT_FORMAT_ARRAY rtv_formats{
      .RTFormats        = { kTransmittanceLUTFormat },
      .NumRenderTargets = 1,
    };

    struct PipelineStream
    {
      CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE        RootSignature;
      CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY    PrimitiveTopologyType;
      CD3DX12_PIPELINE_STATE_STREAM_VS                    VS;
      CD3DX12_PIPELINE_STATE_STREAM_PS                    PS;
      CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS RTVFormats;
    };

    PipelineStream pipeline_stream = {
      .RootSignature         = root_signature.Get(),
      .PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
      .VS                    = CD3DX12_SHADER_BYTECODE( vertex_shader_blob.Get() ),
      .PS                    = CD3DX12_SHADER_BYTECODE( transmittance_pixel_shader_blob.Get() ),
      .RTVFormats            = rtv_formats,
    };

    D3D12_PIPELINE_STATE_STREAM_DESC const pipeline_state_stream_desc = {
      .SizeInBytes                   = sizeof pipeline_stream,
      .pPipelineStateSubobjectStream = &pipeline_stream,
    };

    ERR_FAIL_RET_F(
        device->CreatePipelineState( &pipeline_state_stream_desc, IID_PPV_ARGS( &transmittance_lut_pipeline ) ) );

    pipeline_stream.PS         = CD3DX12_SHADER_BYTECODE( sky_view_pixel_shader_blob.Get() );

    rtv_formats.RTFormats[0]   = kSkyViewLUTFormat;
    pipeline_stream.RTVFormats = rtv_formats;

    ERR_FAIL_RET_F(
        device->CreatePipelineState( &pipeline_state_stream_desc, IID_PPV_ARGS( &sky_view_lut_pipeline ) ) );
  }

  // Aerial Perspective Pipeline
  {
    CD3DX12_ROOT_PARAMETER1 root_parameters[3];
    root_parameters[0].InitAsConstantBufferView( 0 );
    root_parameters[1].InitAsConstants( sizeof( PerFrameConstants ) / 4, 1 );
    root_parameters[2].InitAsConstants( 1, 2 );

    CD3DX12_STATIC_SAMPLER_DESC      static_sampler_desc = CD3DX12_STATIC_SAMPLER_DESC{ 0 };

    D3D12_ROOT_SIGNATURE_FLAGS const root_signature_flags =
        D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED |
        D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_AMPLIFICATION_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_MESH_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS;

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC root_signature_desc;
    root_signature_desc.Init_1_1(
        CountOf( root_parameters ), DataOf( root_parameters ), 1, &static_sampler_desc, root_signature_flags );

    ComPtr<ID3DBlob> root_signature_blob;
    ComPtr<ID3DBlob> error_blob;
    ERR_FAIL_RET_F( D3DX12SerializeVersionedRootSignature(
        &root_signature_desc, root_signature_version, root_signature_blob.ReleaseAndGetAddressOf(), &error_blob ) );

    ERR_FAIL_RET_F( device->CreateRootSignature(
        0,
        root_signature_blob->GetBufferPointer(),
        root_signature_blob->GetBufferSize(),
        IID_PPV_ARGS( &root_signature ) ) );

    struct PipelineStream
    {
      CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE RootSignature;
      CD3DX12_PIPELINE_STATE_STREAM_CS             CS;
    };

    PipelineStream pipeline_stream = {
      .RootSignature = root_signature.Get(),
      .CS            = CD3DX12_SHADER_BYTECODE( aerial_perspective_comp_shader_blob.Get() ),
    };

    D3D12_PIPELINE_STATE_STREAM_DESC const pipeline_state_stream_desc = {
      .SizeInBytes                   = sizeof pipeline_stream,
      .pPipelineStateSubobjectStream = &pipeline_stream,
    };

    ERR_FAIL_RET_F(
        device->CreatePipelineState( &pipeline_state_stream_desc, IID_PPV_ARGS( &aerial_perspective_pipeline ) ) );
  }

  std::vector<Buffer> atmosphere_param_buffers;
  wchar_t             buf[64];
  for ( int i = 0; i < RenderDevice::kNumFrames; ++i )
  {
    Buffer& atmosphere_param_buffer = atmosphere_param_buffers.emplace_back(
        render_device->CreateConstantBuffer( sizeof( Params ) + sizeof( SunData ) ) );
    swprintf_s( buf, L"Atmosphere Parameter CB %d", i );
    atmosphere_param_buffer.SetName( buf );
  }

  new ( out ) AtmosphereContext{
    std::move( root_signature ),
    std::move( transmittance_lut_pipeline ),
    std::move( sky_view_lut_pipeline ),
    std::move( aerial_perspective_root_signature ),
    std::move( aerial_perspective_pipeline ),
    std::move( transmittance_lut ),
    std::move( sky_view_lut ),
    std::move( atmosphere_param_buffers ),
  };

  return true;
}

Ember::AtmosphereContext::OutData Ember::AtmosphereContext::Render(
    FrameGraph* frame_graph, FrameGraphBlackboard* blackboard, uint32_t const frame_idx )
{
  if ( m_LUTUpdatePendingFrames ) m_AtmosphereParamBuffers[frame_idx].Write( 0, sizeof( Params ), &m_AtmosphereParams );
  uint32_t zero = 0;
  m_AtmosphereParamBuffers[frame_idx].Write( sizeof( Params ), sizeof( SunData ), &zero );

  // TODO: Resource layout breaks when sky_view is not rendered, but transmittance is updated.
  // Need to fix by supporting persistent resources in frame graph allocator.
  FrameGraphResource const transmittance_lut = frame_graph->import(
      "Transmittance LUT",
      FG::Texture::Desc{
          .Format    = kTransmittanceLUTFormat,
          .Width     = kTransmittanceLUTSize.x,
          .Height    = kTransmittanceLUTSize.y,
          .MipLevels = MipLevels::kBase,
          .InitState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
          .Flags     = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
      },
      FG::Texture{
          .Resource     = m_TransmittanceLUT.GetTexture(),
          .CurrentState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
      } );

  FrameGraphResource const param_buffer =
      frame_graph->import( "Param Buffer", FG::Buffer::Desc{}, FG::Buffer{ m_AtmosphereParamBuffers[frame_idx] } );

  bool const transmittance_lut_needs_update = m_LUTUpdatePendingFrames;
  if ( m_LUTUpdatePendingFrames ) --m_LUTUpdatePendingFrames;

  OutData const transmittance = frame_graph->addCallbackPass<OutData>(
      "Update Transmittance LUT",
      [&]( FrameGraph::Builder& builder, OutData& data )
      {
        if ( transmittance_lut_needs_update )
        {
          data.TransmittanceLUT = builder.write( transmittance_lut, FG::Attachment{ .Index = 0 } );
        }
        else
        {
          data.TransmittanceLUT = transmittance_lut;
        }
        data.AtmosphereParams = builder.read( param_buffer );
      },
      [mp = this]( OutData const& data, FrameGraphPassResources& resources, void* ctx )
      {
        ZoneScopedN( "Update Transmittance LUT" );

        FG::Context* context = ( FG::Context* )ctx;
        context->FlushBarriers();

        FG::Context::FrameData const& frame_data = context->GetFrameData();
        RenderTargetManager const*    rtm        = context->GetRenderTargetManager();
        ID3D12GraphicsCommandList6*   cmd        = frame_data.CommandList;
        PIXScopedEvent( cmd, PIX_COLOR_DEFAULT, "Update Transmittance LUT" );

        FG::Texture const& render_target = resources.get<FG::Texture>( data.TransmittanceLUT );
        FG::Buffer const&  params        = resources.get<FG::Buffer>( data.AtmosphereParams );

        cmd->DiscardResource( render_target.Resource.Get(), nullptr );
        rtm->RSSetScissorViewport( cmd, kTransmittanceLUTSize.x, kTransmittanceLUTSize.y );

        ID3D12Resource* rtv = render_target.Resource.Get();
        rtm->OMSetRenderTargets( cmd, 1, &rtv, nullptr, nullptr, nullptr );

        cmd->SetGraphicsRootSignature( mp->m_RootSignature.Get() );
        cmd->SetPipelineState( mp->m_TransmittanceLUTPipeline.Get() );
        cmd->SetGraphicsRootConstantBufferView( 0, params.InnerBuffer.GetGPUVirtualAddress() );
        cmd->DrawInstanced( 3, 1, 0, 0 );
      } );

  OutData const sky = frame_graph->addCallbackPass<OutData>(
      "Update Sky View LUT",
      [&]( FrameGraph::Builder& builder, OutData& data )
      {
        FrameGraphResource const sky_view_lut = builder.create<FG::Texture>(
            "Sky View LUT",
            FG::Texture::Desc{
                .Format    = kSkyViewLUTFormat,
                .Width     = kSkyViewLUTSize.x,
                .Height    = kSkyViewLUTSize.y,
                .MipLevels = MipLevels::kBase,
                .InitState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                .Flags     = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
            } );
        data.TransmittanceLUT = builder.read(
            transmittance.TransmittanceLUT,
            FG::ShaderResource{
                .PixelShaderUse = true,
                .OnlyTopMip     = true,
            } );
        data.AtmosphereParams = builder.read( param_buffer );
        data.SkyViewLUT       = builder.write( sky_view_lut, FG::Attachment{ .Index = 0 } );
      },
      [mp = this, blackboard]( OutData const& data, FrameGraphPassResources& resources, void* ctx )
      {
        ZoneScopedN( "Update Sky View LUT" );

        FG::Context* context = ( FG::Context* )ctx;
        context->FlushBarriers();

        FG::Context::FrameData const& frame_data = context->GetFrameData();
        RenderTargetManager const*    rtm        = context->GetRenderTargetManager();
        ID3D12GraphicsCommandList6*   cmd        = frame_data.CommandList;
        PIXScopedEvent( cmd, PIX_COLOR_DEFAULT, "Update Sky View LUT" );

        ID3D12Resource*          sky_view_lut_res  = resources.get<FG::Texture>( data.SkyViewLUT ).Resource.Get();
        FG::Texture const&       transmittance_lut = resources.get<FG::Texture>( data.TransmittanceLUT );
        FG::Buffer const&        params            = resources.get<FG::Buffer>( data.AtmosphereParams );

        PerFrameConstants const& constants         = blackboard->get<PerFrameConstants>();

        cmd->DiscardResource( sky_view_lut_res, nullptr );

        rtm->RSSetScissorViewport( cmd, kSkyViewLUTSize.x, kSkyViewLUTSize.y );

        rtm->OMSetRenderTargets( cmd, 1, &sky_view_lut_res, nullptr, nullptr, nullptr );

        cmd->SetGraphicsRootSignature( mp->m_RootSignature.Get() );
        cmd->SetPipelineState( mp->m_SkyViewLUTPipeline.Get() );
        cmd->SetGraphicsRootConstantBufferView( 0, params.InnerBuffer.GetGPUVirtualAddress() );
        cmd->SetGraphicsRoot32BitConstants( 1, sizeof( PerFrameConstants ) / 4, &constants, 0 );
        cmd->SetGraphicsRoot32BitConstant( 2, ( UINT )transmittance_lut.AsSRV, 0 );
        cmd->DrawInstanced( 3, 1, 0, 0 );
      } );

  return sky;
}

void Ember::AtmosphereContext::SetSun( uint32_t const sun_index )
{
  m_Sun = sun_index;
}

void Ember::AtmosphereContext::SetAtmosphereParams( Params const& atmosphere_params )
{
  m_AtmosphereParams       = atmosphere_params;
  m_LUTUpdatePendingFrames = true;
}

Ember::AtmosphereContext::Params const& Ember::AtmosphereContext::GetAtmosphereParams() const
{
  return m_AtmosphereParams;
}
