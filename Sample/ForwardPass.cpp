#include "ForwardPass.hpp"

#include <Graphics/RenderDevice.hpp>
#include <Util/DataUtil.hpp>
#include <Util/HelperUtils.hpp>
#include <Util/Profiling.hpp>
#include "Environment.hpp"
#include "FrameGraphHelper.hpp"
#include "ReflectionProbe.hpp"
#include "RenderPassCommon.hpp"
#include "Scene.hpp"
#include "fg/Blackboard.hpp"
#include "fg/FrameGraph.hpp"

bool Ember::RenderPass::OpaqueForward::Create( OpaqueForward* out, Desc const& desc )
{
  RenderDevice*             render_device         = desc.RenderDevice;

  D3D12_STATIC_SAMPLER_DESC static_sampler_desc[] = {
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

  D3D12_ROOT_PARAMETER1 root_parameters[] = {
    RootConstants{ .Register = 0, .SizeBytes = sizeof( DrawList::PerBatch ) },
    RootConstantBuffer{ .Register = 1 },
    RootConstants{ .Register = 2, .SizeBytes = sizeof( Environment::GpuRepr ) },
    RootConstants{ .Register = 3, .SizeBytes = sizeof( Proto::ReflectionProbe::Probe ) + 4 },
  };

  ComPtr<ID3D12RootSignature> root_signature =
      desc.RenderDevice->CreateRootSignature( { root_parameters, static_sampler_desc } );
  if ( not root_signature ) return false;

  CD3DX12_DEPTH_STENCIL_DESC depth_stencil_desc{ D3D12_DEFAULT };
  if ( desc.DependsOnDepthPrePass )
  {
    depth_stencil_desc.DepthFunc      = D3D12_COMPARISON_FUNC_EQUAL;
    depth_stencil_desc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
  }
  else
  {
    depth_stencil_desc.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
  }

  ComPtr<ID3D12PipelineState> pipeline = render_device->CreateGraphicsPipeline( {
      .RootSignature    = root_signature.Get(),
      .RTVFormats       = { &desc.RenderTargetFormat, 1 },
      .DepthStencilDesc = depth_stencil_desc,
      .AmpShaderName    = "TriangleAS.cso",
      .MeshShaderName   = "TriangleMS.cso",
      .PixelShaderName  = "TrianglePS.cso",
      .DSVFormat        = desc.DepthStencilFormat,
  } );
  if ( not pipeline ) return false;

  new ( out ) OpaqueForward{
    .RootSignature = std::move( root_signature ),
    .Pipeline      = std::move( pipeline ),
  };

  return true;
}

FrameGraphResource Ember::RenderPass::OpaqueForward::Execute(
    FrameGraph* frame_graph, FrameGraphBlackboard const& bb, FrameGraphResource const depth ) const
{
  auto const root_sig         = RootSignature;
  auto const pipeline         = Pipeline;

  auto const& [constants_buf] = bb.get<FrameConstants>();
  auto const& env             = bb.get<Environment::GpuRepr>();
  auto const& draw_list       = bb.get<DrawList::Batches>();

  return frame_graph->addCallbackPass(
      "Opaque Forward",
      [&]( FrameGraph::Builder& builder, FrameGraphResource& data )
      {
        auto const&              backbuffer_info = bb.get<FG::BackbufferInfo>();
        FrameGraphResource const render_target   = builder.create<FG::Texture>(
            "Main Render Target",
            {
                  .Format    = backbuffer_info.SwapchainFormat,
                  .Width     = backbuffer_info.Width,
                  .Height    = backbuffer_info.Height,
                  .MipLevels = MipLevels::kBase,
                  .Usage     = TextureUsage::kRenderTarget,
                  .InitState = D3D12_RESOURCE_STATE_RENDER_TARGET,
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
      [=]( FrameGraphResource const&, FrameGraphPassResources&, FG::Context const* context )
      {
        ZoneScopedN( "Opaque Forward" );

        FG::Context::FrameData const& frame_data = context->GetFrameData();
        CommandList*                  cmd        = frame_data.CommandList;
        PIXScopedEvent( cmd->Get(), PIX_COLOR_DEFAULT, "Opaque Forward" );

        auto const draw_batch = draw_list.Opaque();

        cmd->SetGraphicsRootSignature( root_sig.Get() );
        cmd->SetPipelineState( pipeline.Get() );
        cmd->SetGraphicsRootConstants( 0, draw_batch );
        cmd->SetGraphicsRootConstantBuffer( 1, constants_buf );
        cmd->SetGraphicsRootConstants( 2, env );
        cmd->SetGraphicsRootConstants( 3, ( UINT )SRVHandle{}, 16 );
        cmd->DispatchMesh( { .X = draw_batch.CommandsCount } );
      } );
}

FrameGraphResource Ember::RenderPass::OpaqueForward::Execute(
    FrameGraph* frame_graph, FrameGraphBlackboard const& bb, Input in ) const
{
  auto const root_sig                 = RootSignature;
  auto const pipeline                 = Pipeline;
  auto [depth, probe_tex, probe_info] = in;

  auto const& [constants_buf]         = bb.get<FrameConstants>();
  auto const& env                     = bb.get<Environment::GpuRepr>();
  auto const& draw_batch              = bb.get<DrawList::Batches>().Opaque();

  return frame_graph->addCallbackPass(
      "Opaque Forward",
      [&]( FrameGraph::Builder& builder, FrameGraphResource& data )
      {
        auto const&              backbuffer_info = bb.get<FG::BackbufferInfo>();
        FrameGraphResource const render_target   = builder.create<FG::Texture>(
            "Main Render Target",
            {
                  .Format    = backbuffer_info.SwapchainFormat,
                  .Width     = backbuffer_info.Width,
                  .Height    = backbuffer_info.Height,
                  .MipLevels = MipLevels::kBase,
                  .Usage     = TextureUsage::kRenderTarget,
                  .InitState = D3D12_RESOURCE_STATE_RENDER_TARGET,
            } );

        data = builder.write(
            render_target,
            FG::Attachment{
                .Index     = 0,
                .ForceSrgb = true,
                .LoadOp    = FG::LoadOperation::kClear,
            } );
        builder.read( depth, FG::DepthStencilRead{} );

        builder.read( probe_tex, FG::ShaderResource{} );
      },
      [=]( FrameGraphResource const&, FrameGraphPassResources& res, FG::Context const* context )
      {
        ZoneScopedN( "Opaque Forward" );

        FG::Context::FrameData const& frame_data = context->GetFrameData();
        CommandList const*            cmd        = frame_data.CommandList;
        PIXScopedEvent( cmd->Get(), PIX_COLOR_DEFAULT, "Opaque Forward" );

        FG::Texture* probe_texture = &res.get<FG::Texture>( probe_tex );

        cmd->SetGraphicsRootSignature( root_sig.Get() );
        cmd->SetPipelineState( pipeline.Get() );
        cmd->SetGraphicsRootConstants( 0, draw_batch );
        cmd->SetGraphicsRootConstantBuffer( 1, constants_buf );
        cmd->SetGraphicsRootConstants( 2, env );
        cmd->SetGraphicsRootConstants( 3, probe_info );
        cmd->SetGraphicsRootConstants( 3, ( UINT )probe_texture->GetSRVHandle(), sizeof( probe_info ) );
        cmd->DispatchMesh( { .X = draw_batch.CommandsCount } );
      } );
}

FrameGraphResource Ember::RenderPass::OpaqueForward::operator()(
    FrameGraph* frame_graph, FrameGraphBlackboard const& bb, FrameGraphResource const depth ) const
{
  return Execute( frame_graph, bb, depth );
}
