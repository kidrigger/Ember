#include "GBufferPass.hpp"

#include "Environment.hpp"
#include "FrameGraphHelper.hpp"
#include "RenderPassCommon.hpp"
#include "Util/DataUtil.hpp"
#include "fg/FrameGraph.hpp"

#include "Util/Profiling.hpp"
#include "fg/Blackboard.hpp"

bool Ember::RenderPass::GBuffer::Create( GBuffer* out, RenderDevice* render_device, DXGI_FORMAT const depth_format )
{
  ComPtr<ID3DBlob> amp_shader_blob;
  ERR_FAIL_RET_F( D3DReadFileToBlob( L"TriangleAS.cso", &amp_shader_blob ) );
  ComPtr<ID3DBlob> mesh_shader_blob;
  ERR_FAIL_RET_F( D3DReadFileToBlob( L"TriangleMS.cso", &mesh_shader_blob ) );
  ComPtr<ID3DBlob> gbuffer_shader_blob;
  ERR_FAIL_RET_F( D3DReadFileToBlob( L"GBufferPS.cso", &gbuffer_shader_blob ) );

  ComPtr<ID3D12Device2>       device                 = render_device->GetDevice();

  D3D_ROOT_SIGNATURE_VERSION  root_signature_version = render_device->FetchHighestRootSignatureVersion();

  CD3DX12_STATIC_SAMPLER_DESC static_sampler_desc[]  = {
    CD3DX12_STATIC_SAMPLER_DESC{ 0 },
    CD3DX12_STATIC_SAMPLER_DESC{ 1,
                                D3D12_FILTER_ANISOTROPIC, D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
                                D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP },
    CD3DX12_STATIC_SAMPLER_DESC{ 2,
                                D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_BORDER,
                                D3D12_TEXTURE_ADDRESS_MODE_BORDER, D3D12_TEXTURE_ADDRESS_MODE_BORDER,
                                0, 16,
                                D3D12_COMPARISON_FUNC_LESS_EQUAL, D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE },
  };

  D3D12_ROOT_SIGNATURE_FLAGS const root_signature_flags = D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED |
                                                          D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED |
                                                          D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS |
                                                          D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
                                                          D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
                                                          D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS;

  CD3DX12_ROOT_PARAMETER1 root_parameters[3];
  root_parameters[0].InitAsConstants( sizeof( DrawList::Info ) / 4, 0 );
  root_parameters[1].InitAsConstants( sizeof( PerFrameConstants ) / 4, 1 );
  root_parameters[2].InitAsConstants( sizeof( Environment::GpuRepr ) / 4, 2 );

  CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC root_signature_desc;
  root_signature_desc.Init_1_1(
      CountOf( root_parameters ),
      DataOf( root_parameters ),
      CountOf( static_sampler_desc ),
      DataOf( static_sampler_desc ),
      root_signature_flags );

  ComPtr<ID3DBlob> root_signature_blob;
  ComPtr<ID3DBlob> error_blob;
  ERR_FAIL_RET_F( D3DX12SerializeVersionedRootSignature(
      &root_signature_desc, root_signature_version, &root_signature_blob, &error_blob ) );

  ERR_FAIL_RET_F( device->CreateRootSignature(
      0,
      root_signature_blob->GetBufferPointer(),
      root_signature_blob->GetBufferSize(),
      IID_PPV_ARGS( out->RootSignature.ReleaseAndGetAddressOf() ) ) );

  D3D12_RT_FORMAT_ARRAY gbuffer_rt_formats{};
  gbuffer_rt_formats.NumRenderTargets = CountOf( kGBufferFormats );
  memcpy( gbuffer_rt_formats.RTFormats, DataOf( kGBufferFormats ), ByteSizeOf( kGBufferFormats ) );

  CD3DX12_RASTERIZER_DESC2 rasterizer_desc{ D3D12_DEFAULT };
  rasterizer_desc.FrontCounterClockwise = TRUE;
  rasterizer_desc.CullMode              = D3D12_CULL_MODE_BACK;

  CD3DX12_DEPTH_STENCIL_DESC depth_stencil_desc{ D3D12_DEFAULT };
  depth_stencil_desc.DepthFunc      = D3D12_COMPARISON_FUNC_EQUAL;
  depth_stencil_desc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;

  struct MainPipelineStream
  {
    CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE        RootSignature;
    CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY    PrimitiveTopologyType;
    CD3DX12_PIPELINE_STATE_STREAM_AS                    AS;
    CD3DX12_PIPELINE_STATE_STREAM_MS                    MS;
    CD3DX12_PIPELINE_STATE_STREAM_PS                    PS;
    CD3DX12_PIPELINE_STATE_STREAM_RASTERIZER2           Rasterizer;
    CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL         DepthStencil;
    CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS RTVFormats;
    CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL_FORMAT  DSVFormat;
  };

  MainPipelineStream pipeline_stream = {
    .RootSignature         = out->RootSignature.Get(),
    .PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
    .AS                    = CD3DX12_SHADER_BYTECODE( amp_shader_blob.Get() ),
    .MS                    = CD3DX12_SHADER_BYTECODE( mesh_shader_blob.Get() ),
    .PS                    = CD3DX12_SHADER_BYTECODE( gbuffer_shader_blob.Get() ),
    .Rasterizer            = rasterizer_desc,
    .DepthStencil          = depth_stencil_desc,
    .RTVFormats            = gbuffer_rt_formats,
    .DSVFormat             = depth_format,
  };

  D3D12_PIPELINE_STATE_STREAM_DESC const pipeline_state_stream_desc = {
    .SizeInBytes                   = sizeof pipeline_stream,
    .pPipelineStateSubobjectStream = &pipeline_stream,
  };

  ERR_FAIL_RET_F( device->CreatePipelineState(
      &pipeline_state_stream_desc, IID_PPV_ARGS( out->Pipeline.ReleaseAndGetAddressOf() ) ) );

  return true;
}

Ember::RenderPass::GBuffer::Data Ember::RenderPass::GBuffer::Execute(
    FrameGraph* frame_graph, FrameGraphBlackboard const& bb, FrameGraphResource const depth_stencil )
{
  return frame_graph->addCallbackPass(
      "GBuffer Pass",
      [&]( FrameGraph::Builder& builder, Data& data )
      {
        auto const&       backbuffer_info = bb.get<FG::BackbufferInfo>();
        FG::Texture::Desc desc{
          .Format    = kGBufferFormats[kPosition],
          .Width     = backbuffer_info.Width,
          .Height    = backbuffer_info.Height,
          .MipLevels = MipLevels::kBase,
          .InitState = D3D12_RESOURCE_STATE_RENDER_TARGET,
          .Flags     = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
        };

        for ( int i = 0; i < kGBufferCount; i++ )
        {
          desc.Format     = kGBufferFormats[i];
          data.GBuffer[i] = builder.create<FG::Texture>( kGBufferNames[i], desc );
          data.GBuffer[i] = builder.write(
              data.GBuffer[i],
              FG::Attachment{
                  .Index  = ( uint8_t )i,
                  .LoadOp = FG::LoadOperation::kClear,
              } );
        }

        data.DepthStencil = builder.read( depth_stencil, FG::DepthStencilRead{} );
      },
      [self = this, bb = &bb]( Data const&, FrameGraphPassResources&, FG::Context const* context )
      {
        ZoneScopedN( "GBuffer Pass" );

        FG::Context::FrameData const& frame_data = context->GetFrameData();
        ID3D12GraphicsCommandList6*   cmd        = frame_data.CommandList;
        PIXScopedEvent( cmd, PIX_COLOR_DEFAULT, "GBuffer Pass" );

        auto const& constants = bb->get<PerFrameConstants>();
        auto const& env       = bb->get<Environment::GpuRepr>();
        auto const& draw_list = bb->get<DrawList::Batches>().Opaque;

        cmd->SetGraphicsRootSignature( self->RootSignature.Get() );
        cmd->SetPipelineState( self->Pipeline.Get() );
        cmd->SetGraphicsRoot32BitConstants( 1, sizeof( constants ) / 4, &constants, 0 );
        cmd->SetGraphicsRoot32BitConstants( 2, sizeof( env ) / 4, &env, 0 );

        cmd->SetGraphicsRoot32BitConstants( 0, sizeof( draw_list ) / 4, &draw_list, 0 );
        cmd->DispatchMesh( draw_list.DrawCount, 1, 1 );
      } );
}

Ember::RenderPass::GBuffer::Data Ember::RenderPass::GBuffer::operator()(
    FrameGraph* frame_graph, FrameGraphBlackboard const& bb, FrameGraphResource const depth_stencil )
{
  return Execute( frame_graph, bb, depth_stencil );
}
