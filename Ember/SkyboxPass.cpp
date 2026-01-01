#include "SkyboxPass.hpp"

#include <Graphics/RenderDevice.hpp>
#include <Util/DataUtil.hpp>
#include <Util/HelperUtils.hpp>
#include <Util/Profiling.hpp>
#include "Environment.hpp"
#include "ForwardPass.hpp"
#include "FrameGraphHelper.hpp"
#include "fg/Blackboard.hpp"
#include "fg/FrameGraph.hpp"

bool Ember::RenderPass::Skybox::Create(
    Skybox* out, RenderDevice* render_device, DXGI_FORMAT const rt_format, DXGI_FORMAT const depth_format )
{
  out->RenderTargetFormat = rt_format;

  ComPtr<ID3DBlob> bg_vertex_shader_blob;
  ERR_FAIL_RET_F( D3DReadFileToBlob( L"BackgroundVS.cso", &bg_vertex_shader_blob ) );
  ComPtr<ID3DBlob> bg_pixel_shader_blob;
  ERR_FAIL_RET_F( D3DReadFileToBlob( L"BackgroundPS.cso", &bg_pixel_shader_blob ) );
  ComPtr<ID3DBlob> atmos_bg_pixel_shader_blob;
  ERR_FAIL_RET_F( D3DReadFileToBlob( L"AtmosphereBackgroundPS.cso", &atmos_bg_pixel_shader_blob ) );

  ComPtr<ID3D12Device2>      device                 = render_device->GetDevice();

  D3D_ROOT_SIGNATURE_VERSION root_signature_version = render_device->FetchHighestRootSignatureVersion();

  CD3DX12_ROOT_PARAMETER1    root_parameters[1];
  root_parameters[0].InitAsConstants( 2, 0 );

  CD3DX12_STATIC_SAMPLER_DESC      static_sampler_desc = CD3DX12_STATIC_SAMPLER_DESC{ 0 };

  D3D12_ROOT_SIGNATURE_FLAGS const root_signature_flags =
      D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED |
      D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED |
      D3D12_ROOT_SIGNATURE_FLAG_DENY_AMPLIFICATION_SHADER_ROOT_ACCESS |
      D3D12_ROOT_SIGNATURE_FLAG_DENY_MESH_SHADER_ROOT_ACCESS |
      D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
      D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS | D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS;

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
      IID_PPV_ARGS( out->RootSignature.ReleaseAndGetAddressOf() ) ) );

  CD3DX12_DEPTH_STENCIL_DESC depth_stencil_desc{ D3D12_DEFAULT };
  depth_stencil_desc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

  D3D12_RT_FORMAT_ARRAY rtv_formats{
    .RTFormats        = { rt_format },
    .NumRenderTargets = 1,
  };

  struct PipelineStream
  {
    CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE        RootSignature;
    CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY    PrimitiveTopologyType;
    CD3DX12_PIPELINE_STATE_STREAM_VS                    VS;
    CD3DX12_PIPELINE_STATE_STREAM_PS                    PS;
    CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL         DepthStencil;
    CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS RTVFormats;
    CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL_FORMAT  DSVFormat;
  };

  PipelineStream pipeline_stream = {
    .RootSignature         = out->RootSignature.Get(),
    .PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
    .VS                    = CD3DX12_SHADER_BYTECODE( bg_vertex_shader_blob.Get() ),
    .PS                    = CD3DX12_SHADER_BYTECODE( bg_pixel_shader_blob.Get() ),
    .DepthStencil          = depth_stencil_desc,
    .RTVFormats            = rtv_formats,
    .DSVFormat             = depth_format,
  };

  D3D12_PIPELINE_STATE_STREAM_DESC const pipeline_state_stream_desc = {
    .SizeInBytes                   = sizeof pipeline_stream,
    .pPipelineStateSubobjectStream = &pipeline_stream,
  };
  ERR_FAIL_RET_F( device->CreatePipelineState(
      &pipeline_state_stream_desc, IID_PPV_ARGS( out->SkyboxPipeline.ReleaseAndGetAddressOf() ) ) );

  pipeline_stream.PS = CD3DX12_SHADER_BYTECODE( atmos_bg_pixel_shader_blob.Get() );
  ERR_FAIL_RET_F( device->CreatePipelineState(
      &pipeline_state_stream_desc, IID_PPV_ARGS( out->AtmospherePipeline.ReleaseAndGetAddressOf() ) ) );

  return true;
}

FrameGraphResource Ember::RenderPass::Skybox::Execute(
    FrameGraph*                 frame_graph,
    FrameGraphBlackboard const& bb,
    RenderDepthData const&      render_depth,
    FrameGraphResource const&   sky_view_lut ) const
{
  Data const& skybox = frame_graph->addCallbackPass(
      "Render Skybox",
      [&]( FrameGraph::Builder& builder, Data& data )
      {
        data.RenderTarget = builder.write( render_depth.RenderTarget, FG::Attachment{ .Index = 0, .ForceSrgb = true } );
        data.DepthStencil = builder.write( render_depth.DepthStencil, FG::DepthStencil{} );

        if ( UseProceduralAtmosphericSky )
        {
          data.SkyViewLUT = builder.read( sky_view_lut, FG::ShaderResource{} );
        }
      },
      [self = this, bb = &bb]( Data const& data, FrameGraphPassResources& resources, FG::Context const* context )
      {
        ZoneScopedN( "Render Skybox" );

        FG::Context::FrameData const& frame_data = context->GetFrameData();
        CommandList*                  cmd        = frame_data.CommandList;

        PIXScopedEvent( cmd->Get(), PIX_COLOR_DEFAULT, "Render Skybox" );

        auto const& constants = bb->get<PerFrameConstants>();
        auto const& env       = bb->get<Environment::GpuRepr>();

        cmd->SetGraphicsRootSignature( self->RootSignature.Get() );
        cmd->SetGraphicsRootConstant( 0, ( UINT )constants.Camera );

        if ( self->UseProceduralAtmosphericSky )
        {
          FG::Texture const& sky_view = resources.get<FG::Texture>( data.SkyViewLUT );
          cmd->SetPipelineState( self->AtmospherePipeline.Get() );
          cmd->SetGraphicsRootConstant( 0, ( UINT )sky_view.AsSRV, 1 );
        }
        else
        {
          cmd->SetPipelineState( self->SkyboxPipeline.Get() );
          cmd->SetGraphicsRootConstant( 0, ( UINT )env.Skybox, 1 );
        }

        cmd->DrawInstanced( 3, 1, 0, 0 );
      } );

  return skybox.RenderTarget;
}

FrameGraphResource Ember::RenderPass::Skybox::operator()(
    FrameGraph*                 frame_graph,
    FrameGraphBlackboard const& bb,
    RenderDepthData const&      depth,
    FrameGraphResource const    sky_view_lut ) const
{
  return Execute( frame_graph, bb, depth, sky_view_lut );
}
