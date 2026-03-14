#include "TransparencyPass.hpp"

#include <Graphics/RenderDevice.hpp>
#include <Util/DataUtil.hpp>
#include <Util/HelperUtils.hpp>
#include <Util/Profiling.hpp>
#include "Environment.hpp"
#include "FrameGraphHelper.hpp"
#include "RenderPassCommon.hpp"
#include "fg/Blackboard.hpp"
#include "fg/FrameGraph.hpp"

bool Ember::RenderPass::TransparencyForward::Create(
    TransparencyForward* out, RenderDevice* render_device, DXGI_FORMAT const rt_format, DXGI_FORMAT const depth_format )
{
  out->RenderTargetFormat                         = rt_format;

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
  };

  ComPtr<ID3D12RootSignature> root_signature = render_device->CreateRootSignature( {
      .RootParameters = root_parameters,
      .StaticSamplers = static_sampler_desc,
      .DebugName      = "Transparency Forward Root Signature",
  } );
  if ( not root_signature ) return false;

  CD3DX12_BLEND_DESC blend_desc{ D3D12_DEFAULT };
  blend_desc.RenderTarget[0] = {
    .BlendEnable           = TRUE,
    .LogicOpEnable         = FALSE,
    .SrcBlend              = D3D12_BLEND_SRC_ALPHA,
    .DestBlend             = D3D12_BLEND_INV_SRC_ALPHA,
    .BlendOp               = D3D12_BLEND_OP_ADD,
    .SrcBlendAlpha         = D3D12_BLEND_ONE,
    .DestBlendAlpha        = D3D12_BLEND_ZERO,
    .BlendOpAlpha          = D3D12_BLEND_OP_ADD,
    .LogicOp               = D3D12_LOGIC_OP_NOOP,
    .RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL,
  };

  ComPtr<ID3D12PipelineState> pipeline = render_device->CreateGraphicsPipeline( {
      .RootSignature   = root_signature.Get(),
      .RTVFormats      = { &rt_format, 1 },
      .BlendDesc       = blend_desc,
      .AmpShaderName   = "TriangleAS.cso",
      .MeshShaderName  = "TriangleMS.cso",
      .PixelShaderName = "TriangleTransparentPS.cso",
      .DSVFormat       = depth_format,
      .DebugName       = "Transparency Forward Pipeline",
  } );
  if ( not pipeline ) return false;

  out->RootSignature = std::move( root_signature );
  out->Pipeline      = std::move( pipeline );

  return true;
}

Ember::RenderPass::RenderDepthData Ember::RenderPass::TransparencyForward::Execute(
    FrameGraph* frame_graph, FrameGraphBlackboard const& bb, RenderDepthData const& render_depth ) const
{
  auto const& pipeline        = Pipeline;
  auto const& root_signature  = RootSignature;

  auto const& [constants_buf] = bb.get<FrameConstants>();
  auto const batch            = bb.get<DrawList::Batches>().Transparent();

  return frame_graph->addCallbackPass(
      "Transparency Pass",
      [&]( FrameGraph::Builder& builder, RenderPass::RenderDepthData& data )
      {
        data.RenderTarget = builder.write( render_depth.RenderTarget, FG::Attachment{ .Index = 0, .ForceSrgb = true } );
        data.DepthStencil = builder.write( render_depth.DepthStencil, FG::DepthStencil{} );
      },
      [=]( RenderDepthData const&, FrameGraphPassResources&, FG::Context* context )
      {
        ZoneScopedN( "Transparency Pass" );

        FG::Context::FrameData const& frame_data = context->GetFrameData();
        CommandList*                  cmd        = frame_data.CommandList;
        PIXScopedEvent( cmd->Get(), PIX_COLOR_DEFAULT, "Transparency Pass" );

        cmd->SetGraphicsRootSignature( root_signature.Get() );
        // TODO: Sort transparent objects back to front
        cmd->SetPipelineState( pipeline.Get() );
        cmd->SetGraphicsRootConstants( 0, batch );
        cmd->SetGraphicsRootConstantBuffer( 1, constants_buf );
        cmd->DispatchMesh( { .X = batch.CommandsCount } );
      } );
}

Ember::RenderPass::RenderDepthData Ember::RenderPass::TransparencyForward::operator()(
    FrameGraph* frame_graph, FrameGraphBlackboard const& bb, RenderDepthData const& render_depth ) const
{
  return Execute( frame_graph, bb, render_depth );
}

bool Ember::RenderPass::MaskedForward::Create(
    MaskedForward* out, RenderDevice* render_device, DXGI_FORMAT const rt_format, DXGI_FORMAT const depth_format )
{
  out->RenderTargetFormat                         = rt_format;

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
  };

  ComPtr<ID3D12RootSignature> root_signature = render_device->CreateRootSignature( {
      .RootParameters = root_parameters,
      .StaticSamplers = static_sampler_desc,
      .DebugName      = "Masked Forward Root Signature",
  } );
  if ( not root_signature ) return false;

  CD3DX12_DEPTH_STENCIL_DESC depth_stencil_desc{ D3D12_DEFAULT };
  depth_stencil_desc.DepthFunc         = D3D12_COMPARISON_FUNC_EQUAL;
  depth_stencil_desc.DepthWriteMask    = D3D12_DEPTH_WRITE_MASK_ZERO;

  ComPtr<ID3D12PipelineState> pipeline = render_device->CreateGraphicsPipeline( {
      .RootSignature    = root_signature.Get(),
      .RTVFormats       = { &rt_format, 1 },
      .DepthStencilDesc = depth_stencil_desc,
      .AmpShaderName    = "TriangleAS.cso",
      .MeshShaderName   = "TriangleMS.cso",
      .PixelShaderName  = "TrianglePS.cso",
      .DSVFormat        = depth_format,
      .DebugName        = "Masked Forward Pipeline",
  } );
  if ( not pipeline ) return false;

  out->RootSignature = std::move( root_signature );
  out->Pipeline      = std::move( pipeline );

  return true;
}

Ember::RenderPass::RenderDepthData Ember::RenderPass::MaskedForward::Execute(
    FrameGraph* frame_graph, FrameGraphBlackboard const& bb, RenderDepthData const& render_depth_data ) const
{
  auto const root_sig         = RootSignature;
  auto const pipeline         = Pipeline;

  auto const& [constants_buf] = bb.get<FrameConstants>();
  auto const& draw_list       = bb.get<DrawList::Batches>();
  auto const  batch           = draw_list.Masked();

  return frame_graph->addCallbackPass(
      "Alpha Tested Pass",
      [&]( FrameGraph::Builder& builder, RenderDepthData& data )
      {
        data.RenderTarget =
            builder.write( render_depth_data.RenderTarget, FG::Attachment{ .Index = 0, .ForceSrgb = true } );
        data.DepthStencil = builder.write( render_depth_data.DepthStencil, FG::DepthStencil{} );
      },
      [=]( RenderDepthData const&, FrameGraphPassResources&, FG::Context const* context )
      {
        ZoneScopedN( "Alpha Tested Pass" );

        FG::Context::FrameData const& frame_data = context->GetFrameData();
        CommandList*                  cmd        = frame_data.CommandList;
        PIXScopedEvent( cmd->Get(), PIX_COLOR_DEFAULT, "Alpha Tested Pass" );

        cmd->SetGraphicsRootSignature( root_sig.Get() );
        cmd->SetPipelineState( pipeline.Get() );
        cmd->SetGraphicsRootConstants( 0, batch );
        cmd->SetGraphicsRootConstantBuffer( 1, constants_buf );
        cmd->DispatchMesh( { .X = batch.CommandsCount } );
      } );
}

Ember::RenderPass::RenderDepthData Ember::RenderPass::MaskedForward::operator()(
    FrameGraph* frame_graph, FrameGraphBlackboard const& bb, RenderDepthData const& render_depth ) const
{
  return Execute( frame_graph, bb, render_depth );
}
