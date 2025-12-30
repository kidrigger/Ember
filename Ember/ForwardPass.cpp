#include "ForwardPass.hpp"

#include <Graphics/RenderDevice.hpp>
#include <Util/DataUtil.hpp>
#include <Util/HelperUtils.hpp>
#include <Util/Profiling.hpp>
#include "Environment.hpp"
#include "FrameGraphHelper.hpp"
#include "RenderPassCommon.hpp"
#include "Scene.hpp"
#include "fg/Blackboard.hpp"
#include "fg/FrameGraph.hpp"


bool Ember::RenderPass::OpaqueForward::Create( OpaqueForward* out, Desc const& desc )
{
  ComPtr<ID3DBlob> amp_shader_blob;
  ERR_FAIL_RET_F( D3DReadFileToBlob( L"TriangleAS.cso", &amp_shader_blob ) );
  ComPtr<ID3DBlob> mesh_shader_blob;
  ERR_FAIL_RET_F( D3DReadFileToBlob( L"TriangleMS.cso", &mesh_shader_blob ) );
  ComPtr<ID3DBlob> pixel_shader_blob;
  ERR_FAIL_RET_F( D3DReadFileToBlob( L"TrianglePS.cso", &pixel_shader_blob ) );

  ID3D12Device2*              device                 = desc.RenderDevice->GetDevice();

  D3D_ROOT_SIGNATURE_VERSION  root_signature_version = desc.RenderDevice->FetchHighestRootSignatureVersion();

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

  D3D12_RT_FORMAT_ARRAY rtv_formats{
    .RTFormats        = { desc.RenderTargetFormat },
    .NumRenderTargets = 1,
  };

  CD3DX12_RASTERIZER_DESC2 rasterizer_desc{ D3D12_DEFAULT };
  rasterizer_desc.FrontCounterClockwise = TRUE;
  rasterizer_desc.CullMode              = D3D12_CULL_MODE_BACK;

  CD3DX12_DEPTH_STENCIL_DESC depth_stencil_desc{ D3D12_DEFAULT };
  if ( desc.DependsOnDepthPrePass )
  {
    // We read from depth as in pre-pass.
    depth_stencil_desc.DepthFunc      = D3D12_COMPARISON_FUNC_EQUAL;
    depth_stencil_desc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
  }
  else
  {
    depth_stencil_desc.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
  }

  struct PipelineStream
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

  PipelineStream pipeline_stream = {
    .RootSignature         = out->RootSignature.Get(),
    .PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
    .AS                    = CD3DX12_SHADER_BYTECODE( amp_shader_blob.Get() ),
    .MS                    = CD3DX12_SHADER_BYTECODE( mesh_shader_blob.Get() ),
    .PS                    = CD3DX12_SHADER_BYTECODE( pixel_shader_blob.Get() ),
    .Rasterizer            = rasterizer_desc,
    .DepthStencil          = depth_stencil_desc,
    .RTVFormats            = rtv_formats,
    .DSVFormat             = desc.DepthStencilFormat,
  };

  D3D12_PIPELINE_STATE_STREAM_DESC const pipeline_state_stream_desc = {
    .SizeInBytes                   = sizeof pipeline_stream,
    .pPipelineStateSubobjectStream = &pipeline_stream,
  };

  ERR_FAIL_RET_F( device->CreatePipelineState(
      &pipeline_state_stream_desc, IID_PPV_ARGS( out->Pipeline.ReleaseAndGetAddressOf() ) ) );

  return true;
}

FrameGraphResource Ember::RenderPass::OpaqueForward::Execute(
    FrameGraph* frame_graph, FrameGraphBlackboard const& bb, FrameGraphResource const depth ) const
{
  return frame_graph->addCallbackPass(
      "Opaque Forward",
      [&]( FrameGraph::Builder& builder, FrameGraphResource& data )
      {
        auto const&              backbuffer_info = bb.get<FG::BackbufferInfo>();
        FrameGraphResource const render_target   = builder.create<FG::Texture>(
            "Main Render Target",
            FG::Texture::Desc{
                  .Format    = backbuffer_info.SwapchainFormat,
                  .Width     = backbuffer_info.Width,
                  .Height    = backbuffer_info.Height,
                  .MipLevels = MipLevels::kBase,
                  .InitState = D3D12_RESOURCE_STATE_RENDER_TARGET,
                  .Flags     = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
            } );

        data = builder.write(
            render_target,
            FG::Attachment{
                .Index     = 0,
                .ForceSrgb = true,
                .LoadOp    = FG::LoadOperation::kClear,
            } );
        builder.read( depth, FG::DepthStencilRead{} );
      },
      [self = this, bb = &bb]( FrameGraphResource const&, FrameGraphPassResources&, FG::Context const* context )
      {
        ZoneScopedN( "Opaque Forward" );

        FG::Context::FrameData const& frame_data = context->GetFrameData();
        CommandList*                  cmd        = frame_data.CommandList;
        PIXScopedEvent( cmd->Get(), PIX_COLOR_DEFAULT, "Opaque Forward" );

        auto const& constants = bb->get<PerFrameConstants>();
        auto const& env       = bb->get<Environment::GpuRepr>();
        auto const& draw_list = bb->get<DrawList::Batches>().Opaque;

        cmd->SetGraphicsRootSignature( self->RootSignature.Get() );
        cmd->SetGraphicsRootConstants( 1, constants );
        cmd->SetGraphicsRootConstants( 2, env );

        cmd->SetPipelineState( self->Pipeline.Get() );
        cmd->SetGraphicsRootConstants( 0, draw_list );
        cmd->DispatchMesh( { .X = draw_list.DrawCount } );
      } );
}

FrameGraphResource Ember::RenderPass::OpaqueForward::operator()(
    FrameGraph* frame_graph, FrameGraphBlackboard const& bb, FrameGraphResource const depth ) const
{
  return Execute( frame_graph, bb, depth );
}
