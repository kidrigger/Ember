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
  D3D12_ROOT_PARAMETER1 root_parameters[] = {
    RootConstantBuffer{ .Register = 0 },
  };

  D3D12_STATIC_SAMPLER_DESC   static_sampler_desc[] = { CD3DX12_STATIC_SAMPLER_DESC{ 0 } };

  ComPtr<ID3D12RootSignature> root_signature        = render_device->CreateRootSignature( {
             .RootParameters = root_parameters,
             .StaticSamplers = static_sampler_desc,
             .ShaderAccess   = RootSignatureDesc::Access::kVertexPixel,
             .DebugName      = "Skybox Root Signature",
  } );
  if ( not root_signature ) return false;

  CD3DX12_DEPTH_STENCIL_DESC depth_stencil_desc{ D3D12_DEFAULT };
  depth_stencil_desc.DepthFunc                = D3D12_COMPARISON_FUNC_LESS_EQUAL;

  ComPtr<ID3D12PipelineState> skybox_pipeline = render_device->CreateGraphicsPipeline( {
      .RootSignature    = root_signature.Get(),
      .RTVFormats       = { &rt_format, 1 },
      .RasterizerDesc   = Rasterizer{ .FrontFace = Rasterizer::FrontFace::kClockwise },
      .DepthStencilDesc = depth_stencil_desc,
      .VertexShaderName = "BackgroundVS.cso",
      .PixelShaderName  = "BackgroundPS.cso",
      .DSVFormat        = depth_format,
      .DebugName        = "Skybox Pipeline",
  } );
  if ( not skybox_pipeline ) return false;

  *out = {
    .RootSignature      = std::move( root_signature ),
    .SkyboxPipeline     = std::move( skybox_pipeline ),
    .RenderTargetFormat = rt_format,
  };

  return true;
}

FrameGraphResource Ember::RenderPass::Skybox::Execute(
    FrameGraph* frame_graph, FrameGraphBlackboard const& bb, RenderDepthData const& render_depth ) const
{

  auto const& [constants_buf] = bb.get<FrameConstants>();
  auto const& root_sig        = RootSignature;
  auto const& skybox_pipeline = SkyboxPipeline;

  Data const& skybox          = frame_graph->addCallbackPass(
      "Render Skybox",
      [&]( FrameGraph::Builder& builder, Data& data )
      {
        data.RenderTarget = builder.write( render_depth.RenderTarget, FG::Attachment{ .Index = 0, .ForceSrgb = true } );
        data.DepthStencil = builder.write( render_depth.DepthStencil, FG::DepthStencil{} );
      },
      [=]( Data const&, FrameGraphPassResources&, FG::Context const* context )
      {
        ZoneScopedN( "Render Skybox" );

        FG::Context::FrameData const& frame_data = context->GetFrameData();
        CommandList*                  cmd        = frame_data.CommandList;

        PIXScopedEvent( cmd->Get(), PIX_COLOR_DEFAULT, "Render Skybox" );

        cmd->SetGraphicsRootSignature( root_sig.Get() );
        cmd->SetGraphicsRootConstantBuffer( 0, constants_buf );
        cmd->IASetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST );

        cmd->SetPipelineState( skybox_pipeline.Get() );
        cmd->DrawInstanced( 3, 1, 0, 0 );
      } );

  return skybox.RenderTarget;
}

FrameGraphResource Ember::RenderPass::Skybox::operator()(
    FrameGraph* frame_graph, FrameGraphBlackboard const& bb, RenderDepthData const& depth ) const
{
  return Execute( frame_graph, bb, depth );
}

bool Ember::RenderPass::AtmosphereSkybox::Create(
    AtmosphereSkybox* out, RenderDevice* render_device, DXGI_FORMAT rt_format, DXGI_FORMAT depth_format )
{
  out->RenderTargetFormat                 = rt_format;

  D3D12_ROOT_PARAMETER1 root_parameters[] = {
    RootConstantBuffer{ .Register = 0 },
    RootConstants{ .Register = 1, .SizeBytes = 1 * 4 },
  };

  D3D12_STATIC_SAMPLER_DESC   static_sampler_desc[] = { CD3DX12_STATIC_SAMPLER_DESC{ 0 } };

  ComPtr<ID3D12RootSignature> root_signature        = render_device->CreateRootSignature( {
             .RootParameters = root_parameters,
             .StaticSamplers = static_sampler_desc,
             .ShaderAccess   = RootSignatureDesc::Access::kVertexPixel,
             .DebugName      = "Skybox Root Signature",
  } );
  if ( not root_signature ) return false;

  CD3DX12_DEPTH_STENCIL_DESC depth_stencil_desc{ D3D12_DEFAULT };
  depth_stencil_desc.DepthFunc                    = D3D12_COMPARISON_FUNC_LESS_EQUAL;

  ComPtr<ID3D12PipelineState> atmosphere_pipeline = render_device->CreateGraphicsPipeline( {
      .RootSignature    = root_signature.Get(),
      .RTVFormats       = { &rt_format, 1 },
      .RasterizerDesc   = Rasterizer{ .FrontFace = Rasterizer::FrontFace::kClockwise },
      .DepthStencilDesc = depth_stencil_desc,
      .VertexShaderName = "BackgroundVS.cso",
      .PixelShaderName  = "AtmosphereBackgroundPS.cso",
      .DSVFormat        = depth_format,
      .DebugName        = "Atmosphere Background Pipeline",
  } );
  if ( not atmosphere_pipeline ) return false;

  out->RootSignature      = std::move( root_signature );
  out->AtmospherePipeline = std::move( atmosphere_pipeline );

  return true;
}

FrameGraphResource Ember::RenderPass::AtmosphereSkybox::Execute(
    FrameGraph*                 frame_graph,
    FrameGraphBlackboard const& bb,
    RenderDepthData const&      render_depth,
    FrameGraphResource const    sky_view_lut ) const
{
  auto const& [constants_buf] = bb.get<FrameConstants>();
  auto const& root_sig        = RootSignature;
  auto const& atmos_pipeline  = AtmospherePipeline;

  Data const& skybox          = frame_graph->addCallbackPass(
      "Render Atmospheric Skybox",
      [&]( FrameGraph::Builder& builder, Data& data )
      {
        data.RenderTarget = builder.write( render_depth.RenderTarget, FG::Attachment{ .Index = 0, .ForceSrgb = true } );
        data.DepthStencil = builder.write( render_depth.DepthStencil, FG::DepthStencil{} );
        data.SkyViewLUT   = builder.read( sky_view_lut, FG::ShaderRead{} );
      },
      [=]( Data const& data, FrameGraphPassResources& resources, FG::Context const* context )
      {
        ZoneScopedN( "Render Atmospheric Skybox" );

        FG::Context::FrameData const& frame_data = context->GetFrameData();
        CommandList*                  cmd        = frame_data.CommandList;

        PIXScopedEvent( cmd->Get(), PIX_COLOR_DEFAULT, "Render Atmospheric Skybox" );

        cmd->SetGraphicsRootSignature( root_sig.Get() );
        cmd->SetGraphicsRootConstantBuffer( 0, constants_buf );
        cmd->IASetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST );

        FG::Texture const& sky_view = resources.get<FG::Texture>( data.SkyViewLUT );
        cmd->SetPipelineState( atmos_pipeline.Get() );
        cmd->SetGraphicsRootConstant( 1, ( UINT )sky_view.GetSRVHandle() );
        cmd->DrawInstanced( 3, 1, 0, 0 );
      } );

  return skybox.RenderTarget;
}

FrameGraphResource Ember::RenderPass::AtmosphereSkybox::operator()(
    FrameGraph*                 frame_graph,
    FrameGraphBlackboard const& bb,
    RenderDepthData const&      depth,
    FrameGraphResource const    sky_view_lut ) const
{
  return Execute( frame_graph, bb, depth, sky_view_lut );
}
