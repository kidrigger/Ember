#include "DepthPrePass.hpp"

#include <Util/DataUtil.hpp>
#include <Util/HelperUtils.hpp>
#include <Util/Profiling.hpp>
#include "FrameGraphHelper.hpp"
#include "Render/DrawList.hpp"
#include "Scene.hpp"
#include "fg/Blackboard.hpp"
#include "fg/FrameGraph.hpp"

Ember::RenderPass::DepthPrePass::DepthPrePass(
    ComPtr<ID3D12RootSignature> root_signature,
    ComPtr<ID3D12PipelineState> opaque_pipeline,
    ComPtr<ID3D12PipelineState> alpha_tested_pipeline )
  : m_RootSignature{ std::move( root_signature ) }
  , m_OpaquePipeline{ std::move( opaque_pipeline ) }
  , m_MaskedPipeline{ std::move( alpha_tested_pipeline ) }
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
            {
                   .Format    = backbuffer_info.DepthStencilFormat,
                   .Width     = backbuffer_info.Width,
                   .Height    = backbuffer_info.Height,
                   .MipLevels = MipLevels::kBase,
                   .Usage     = TextureUsage::kDepthStencil,
                   .InitState = D3D12_RESOURCE_STATE_DEPTH_WRITE,
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

        DrawList::Batches const& draw_list = blackboard.get<DrawList::Batches>();
        auto const& [constants_buf]        = blackboard.get<FrameConstants>();

        cmd->SetGraphicsRootSignature( this->m_RootSignature.Get() );
        cmd->SetGraphicsRootConstantBuffer( 1, constants_buf );

        auto const opaque_batch = draw_list.Opaque();

        cmd->SetPipelineState( this->m_OpaquePipeline.Get() );
        cmd->SetGraphicsRootConstants( 0, opaque_batch );
        cmd->DispatchMesh( { .X = opaque_batch.CommandsCount } );

        auto const masked_batch = draw_list.Masked();

        cmd->SetPipelineState( this->m_MaskedPipeline.Get() );
        cmd->SetGraphicsRootConstants( 0, masked_batch );
        cmd->DispatchMesh( { .X = masked_batch.CommandsCount } );
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
  D3D12_ROOT_PARAMETER1 root_parameter[] = {
    RootConstants{ .Register = 0, .SizeBytes = sizeof( DrawList::PerBatch ) },
    RootConstantBuffer{ .Register = 1 },
  };

  D3D12_STATIC_SAMPLER_DESC   static_sampler_desc[] = { CD3DX12_STATIC_SAMPLER_DESC{ 0 } };

  ComPtr<ID3D12RootSignature> shadow_root_sig       = render_device->CreateRootSignature( {
            .RootParameters = root_parameter,
            .StaticSamplers = static_sampler_desc,
            .DebugName      = "Depth PrePass",
  } );

  auto                        opaque_pipeline       = render_device->CreateGraphicsPipeline( {
                                   .RootSignature   = shadow_root_sig.Get(),
                                   .AmpShaderName   = "DepthPrePassAS.cso",
                                   .MeshShaderName  = "DepthPrePassMS.cso",
                                   .PixelShaderName = "EmptyPS.cso",
                                   .DSVFormat       = depth_format,
                                   .DebugName       = "Depth PrePass Opaque Pipeline",
  } );

  auto                        alpha_tested_pipeline = render_device->CreateGraphicsPipeline( {
                             .RootSignature   = shadow_root_sig.Get(),
                             .AmpShaderName   = "DepthPrePassAS.cso",
                             .MeshShaderName  = "DepthPrePassMS.cso",
                             .PixelShaderName = "DepthPrePassMaskedPS.cso",
                             .DSVFormat       = depth_format,
                             .DebugName       = "Depth PrePass Alpha Tested Pipeline",
  } );

  new ( out )
      DepthPrePass{ std::move( shadow_root_sig ), std::move( opaque_pipeline ), std::move( alpha_tested_pipeline ) };

  return true;
}
