#include "DepthPrePass.hpp"

#include <Util/DataUtil.hpp>
#include <Util/HelperUtils.hpp>
#include <Util/Profiling.hpp>
#include "FrameGraphHelper.hpp"
#include "Scene.hpp"
#include "fg/Blackboard.hpp"
#include "fg/FrameGraph.hpp"

Ember::RenderPass::DepthPrePass::DepthPrePass(
    ComPtr<ID3D12RootSignature> root_signature,
    ComPtr<ID3D12PipelineState> opaque_pipeline,
    ComPtr<ID3D12PipelineState> alpha_tested_pipeline )
  : m_RootSignature{ std::move( root_signature ) }
  , m_OpaquePipeline{ std::move( opaque_pipeline ) }
  , m_AlphaTestedPipeline{ std::move( alpha_tested_pipeline ) }
{}

FrameGraphResource Ember::RenderPass::DepthPrePass::Execute(
    FrameGraph* frame_graph, FrameGraphBlackboard const& blackboard ) const
{
  return frame_graph->addCallbackPass(
      "Depth PrePass",
      [&]( FrameGraph::Builder& builder, FrameGraphResource& out_depth_texture )
      {
        auto const& backbuffer_info = blackboard.get<FG::BackbufferInfo>();
        auto const  depth_texture   = builder.create<FG::Texture>(
            "Main Depth Target",
            FG::Texture::Desc{
                   .Format    = backbuffer_info.DepthStencilFormat,
                   .Width     = backbuffer_info.Width,
                   .Height    = backbuffer_info.Height,
                   .MipLevels = MipLevels::kBase,
                   .InitState = D3D12_RESOURCE_STATE_DEPTH_WRITE,
                   .Flags     = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL,
            } );

        out_depth_texture = builder.write(
            depth_texture,
            FG::DepthStencil{
                .LoadOp = FG::LoadOperation::kClear,
            } );
      },
      [this, &blackboard]( FrameGraphResource const&, FrameGraphPassResources&, FG::Context const* context )
      {
        ZoneScopedN( "Depth PrePass" );

        FG::Context::FrameData const& frame_data = context->GetFrameData();
        CommandList*                  cmd        = frame_data.CommandList;
        PIXScopedEvent( cmd->Get(), PIX_COLOR_DEFAULT, "Depth PrePass" );

        DrawList::Batches const& draw_list_info_list = blackboard.get<DrawList::Batches>();
        PerFrameConstants const& constants           = blackboard.get<PerFrameConstants>();

        cmd->SetGraphicsRootSignature( this->m_RootSignature.Get() );
        cmd->SetGraphicsRootConstants( 1, constants );

        cmd->SetPipelineState( this->m_OpaquePipeline.Get() );
        cmd->SetGraphicsRootConstants( 0, draw_list_info_list.Opaque );
        cmd->DispatchMesh( { .X = draw_list_info_list.Opaque.DrawCount } );

        cmd->SetPipelineState( this->m_AlphaTestedPipeline.Get() );
        cmd->SetGraphicsRootConstants( 0, draw_list_info_list.AlphaTested );
        cmd->DispatchMesh( { .X = draw_list_info_list.AlphaTested.DrawCount } );
      } );
}

FrameGraphResource Ember::RenderPass::DepthPrePass::operator()(
    FrameGraph* frame_graph, FrameGraphBlackboard const& blackboard ) const
{
  return Execute( frame_graph, blackboard );
}

bool Ember::RenderPass::DepthPrePass::Create(
    DepthPrePass* out, RenderDevice* render_device, DXGI_FORMAT const depth_format )
{
  ComPtr<ID3DBlob> amp_shader;
  ERR_FAIL_RET_F( D3DReadFileToBlob( L"DepthPrePassAS.cso", &amp_shader ) );

  ComPtr<ID3DBlob> mesh_shader;
  ERR_FAIL_RET_F( D3DReadFileToBlob( L"DepthPrePassMS.cso", &mesh_shader ) );

  ComPtr<ID3DBlob> opaque_pixel_shader;
  ERR_FAIL_RET_F( D3DReadFileToBlob( L"EmptyPS.cso", &opaque_pixel_shader ) );

  ComPtr<ID3DBlob> alpha_tested_pixel_shader;
  ERR_FAIL_RET_F( D3DReadFileToBlob( L"DepthPrePassAlphaTestedPS.cso", &alpha_tested_pixel_shader ) );

  D3D12_ROOT_SIGNATURE_FLAGS const root_signature_flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
                                                          D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED |
                                                          D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED |
                                                          D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
                                                          D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS |
                                                          D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
                                                          D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS;

  CD3DX12_ROOT_PARAMETER1 root_parameter[2];
  root_parameter[0].InitAsConstants( sizeof( DrawList::Info ) / 4, 0 );
  root_parameter[1].InitAsConstants( sizeof( PerFrameConstants ) / 4, 1 );

  CD3DX12_STATIC_SAMPLER_DESC           static_sampler_desc{ 0 };

  CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC root_signature_desc;
  root_signature_desc.Init_1_1(
      CountOf( root_parameter ), DataOf( root_parameter ), 1, &static_sampler_desc, root_signature_flags );

  D3D_ROOT_SIGNATURE_VERSION root_signature_version = render_device->FetchHighestRootSignatureVersion();

  ComPtr<ID3DBlob>           root_signature_blob;
  ComPtr<ID3DBlob>           error_blob;
  ERR_FAIL_RET_F( D3DX12SerializeVersionedRootSignature(
      &root_signature_desc, root_signature_version, &root_signature_blob, &error_blob ) );

  ComPtr<ID3D12RootSignature> shadow_root_sig;
  ERR_FAIL_RET_F( render_device->GetDevice()->CreateRootSignature(
      0,
      root_signature_blob->GetBufferPointer(),
      root_signature_blob->GetBufferSize(),
      IID_PPV_ARGS( &shadow_root_sig ) ) );

  CD3DX12_RASTERIZER_DESC2 rasterizer_desc{ D3D12_DEFAULT };
  rasterizer_desc.FrontCounterClockwise = TRUE;
  rasterizer_desc.CullMode              = D3D12_CULL_MODE_BACK;

  struct PipelineStream
  {
    CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE       RootSignature;
    CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY   PrimitiveTopologyType;
    CD3DX12_PIPELINE_STATE_STREAM_AS                   AS;
    CD3DX12_PIPELINE_STATE_STREAM_MS                   MS;
    CD3DX12_PIPELINE_STATE_STREAM_PS                   PS;
    CD3DX12_PIPELINE_STATE_STREAM_RASTERIZER2          Rasterizer;
    CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL_FORMAT DSVFormat;
  };

  PipelineStream pipeline_stream{
    .RootSignature         = shadow_root_sig.Get(),
    .PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
    .AS                    = CD3DX12_SHADER_BYTECODE( amp_shader.Get() ),
    .MS                    = CD3DX12_SHADER_BYTECODE( mesh_shader.Get() ),
    .PS                    = CD3DX12_SHADER_BYTECODE( opaque_pixel_shader.Get() ),
    .Rasterizer            = rasterizer_desc,
    .DSVFormat             = depth_format,
  };

  D3D12_PIPELINE_STATE_STREAM_DESC desc{
    .SizeInBytes                   = sizeof( pipeline_stream ),
    .pPipelineStateSubobjectStream = &pipeline_stream,
  };

  ComPtr<ID3D12PipelineState> opaque_pipeline;
  ERR_FAIL_RET_F( render_device->GetDevice()->CreatePipelineState( &desc, IID_PPV_ARGS( &opaque_pipeline ) ) );
  ERR_FAIL_RET_F( opaque_pipeline->SetName( L"Depth PrePass Opaque Pipeline" ) );

  pipeline_stream.PS = CD3DX12_SHADER_BYTECODE( alpha_tested_pixel_shader.Get() );
  ComPtr<ID3D12PipelineState> alpha_tested_pipeline;
  ERR_FAIL_RET_F( render_device->GetDevice()->CreatePipelineState( &desc, IID_PPV_ARGS( &alpha_tested_pipeline ) ) );
  ERR_FAIL_RET_F( alpha_tested_pipeline->SetName( L"Depth PrePass Alpha Tested Pipeline" ) );

  new ( out )
      DepthPrePass{ std::move( shadow_root_sig ), std::move( opaque_pipeline ), std::move( alpha_tested_pipeline ) };

  return true;
}
