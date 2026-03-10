#include "LightingPass.hpp"

#include <Util/DataUtil.hpp>
#include <Util/Profiling.hpp>
#include "Environment.hpp"
#include "ForwardPass.hpp"
#include "FrameGraphHelper.hpp"
#include "RenderPassCommon.hpp"
#include "fg/FrameGraph.hpp"

bool Ember::RenderPass::OmniLightDeferred::Create(
    OmniLightDeferred* out, RenderDevice* render_device, DXGI_FORMAT const rt_format, DXGI_FORMAT const depth_format )
{
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
    CD3DX12_STATIC_SAMPLER_DESC{ 3, D3D12_FILTER_MIN_MAG_MIP_POINT },
  };

  D3D12_ROOT_PARAMETER1 root_parameters[] = {
    RootConstants{ .Register = 0, .SizeBytes = 7 * 4 },
    RootConstantBuffer{ .Register = 1 },
  };

  ComPtr<ID3D12RootSignature> root_signature = render_device->CreateRootSignature( {
      .RootParameters = root_parameters,
      .StaticSamplers = static_sampler_desc,
      .DebugName      = "Omni Light Deferred Root Signature",
  } );
  if ( not root_signature ) return false;

  CD3DX12_DEPTH_STENCIL_DESC depth_stencil_desc{ D3D12_DEFAULT };
  depth_stencil_desc.DepthEnable    = TRUE;
  depth_stencil_desc.DepthFunc      = D3D12_COMPARISON_FUNC_GREATER_EQUAL;
  depth_stencil_desc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;

  CD3DX12_BLEND_DESC blend_desc{ D3D12_DEFAULT };
  blend_desc.RenderTarget[0] = {
    .BlendEnable           = TRUE,
    .LogicOpEnable         = FALSE,
    .SrcBlend              = D3D12_BLEND_ONE,
    .DestBlend             = D3D12_BLEND_ONE,
    .BlendOp               = D3D12_BLEND_OP_ADD,
    .SrcBlendAlpha         = D3D12_BLEND_ONE,
    .DestBlendAlpha        = D3D12_BLEND_ONE,
    .BlendOpAlpha          = D3D12_BLEND_OP_ADD,
    .LogicOp               = D3D12_LOGIC_OP_NOOP,
    .RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL,
  };

  ComPtr<ID3D12PipelineState> pipeline = render_device->CreateGraphicsPipeline( {
      .RootSignature    = root_signature.Get(),
      .RTVFormats       = { &rt_format, 1 },
      .RasterizerDesc   = Rasterizer{ .CullMode = Rasterizer::CullMode::kFront },
      .DepthStencilDesc = depth_stencil_desc,
      .BlendDesc        = blend_desc,
      .AmpShaderName    = "OmniLightingAS.cso",
      .MeshShaderName   = "OmniLightingMS.cso",
      .PixelShaderName  = "OmniLightingPS.cso",
      .DSVFormat        = depth_format,
      .DebugName        = "Omni Light Deferred Pipeline",
  } );
  if ( not pipeline ) return false;

  out->RootSignature = std::move( root_signature );
  out->Pipeline      = std::move( pipeline );

  return true;
}

namespace Ember::RenderPass
{
struct MergeData
{
  std::array<FrameGraphResource, GBuffer::kGBufferCount> GBuffer;
  FrameGraphResource                                     RenderTarget;
  FrameGraphResource                                     DepthStencil;
  FrameGraphResource                                     SSAO;
};
} // namespace Ember::RenderPass

FrameGraphResource Ember::RenderPass::OmniLightDeferred::Execute(
    FrameGraph* frame_graph, FrameGraphBlackboard const& bb, GBuffer::Data const& gbuffer ) const
{
  auto const omni_light_count = bb.get<LightManager::GpuRepr>().OmniLightInfo.TotalLightCount;
  auto const& [constants_buf] = bb.get<FrameConstants>();
  auto const&      root_sig   = RootSignature;
  auto const       pipeline   = Pipeline;

  MergeData const& result     = frame_graph->addCallbackPass(
      "OmniLight Pass",
      [&]( FrameGraph::Builder& builder, MergeData& data )
      {
        for ( uint32_t i = 0; i < GBuffer::kGBufferCount; i++ )
        {
          data.GBuffer[i] = builder.read( gbuffer.GBuffer[i], FG::ShaderRead{} );
        }

        auto const&              backbuffer_info = bb.get<FG::BackbufferInfo>();
        FrameGraphResource const render_target   = builder.create<FG::Texture>(
            "Main Render Target",
            {
                      .Format    = backbuffer_info.SwapchainFormat,
                      .Width     = backbuffer_info.Width,
                      .Height    = backbuffer_info.Height,
                      .MipLevels = MipLevels::kBase,
                      .Type      = TextureType::kRenderTarget,
                      .InitState = D3D12_RESOURCE_STATE_RENDER_TARGET,
            } );

        data.RenderTarget = builder.write(
            render_target,
            FG::Attachment{
                    .Index     = 0,
                    .ForceSrgb = true,
                    .LoadOp    = FG::LoadOperation::kClear,
            } );
        data.DepthStencil = builder.read( gbuffer.DepthStencil, FG::DepthStencilRead{} );
      },
      [=]( MergeData const& data, FrameGraphPassResources& resources, FG::Context const* context )
      {
        ZoneScopedN( "OmniLight Pass" );

        FG::Context::FrameData const& frame_data = context->GetFrameData();
        CommandList*                  cmd        = frame_data.CommandList;
        PIXScopedEvent( cmd->Get(), PIX_COLOR_DEFAULT, "OmniLight Pass" );

        SRVHandle gbuffer_handles[GBuffer::kGBufferCount];
        for ( uint32_t i = 0; i < GBuffer::kGBufferCount; i++ )
        {
          gbuffer_handles[i] = resources.get<FG::Texture>( data.GBuffer[i] ).GetSRVHandle();
        }

        cmd->SetGraphicsRootSignature( root_sig.Get() );
        cmd->SetPipelineState( pipeline.Get() );
        cmd->SetGraphicsRootConstants( 0, gbuffer_handles );
        cmd->SetGraphicsRootConstantBuffer( 1, constants_buf );
        cmd->DispatchMesh( { .X = ( omni_light_count + 31 ) / 32 } );
      } );
  return result.RenderTarget;
}

FrameGraphResource Ember::RenderPass::OmniLightDeferred::operator()(
    FrameGraph* frame_graph, FrameGraphBlackboard const& bb, GBuffer::Data const& gbuffer ) const
{
  return Execute( frame_graph, bb, gbuffer );
}

bool Ember::RenderPass::SpotLightDeferred::Create(
    SpotLightDeferred* out, RenderDevice* render_device, DXGI_FORMAT const rt_format, DXGI_FORMAT const depth_format )
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
    CD3DX12_STATIC_SAMPLER_DESC{ 3, D3D12_FILTER_MIN_MAG_MIP_POINT },
  };

  D3D12_ROOT_PARAMETER1 root_parameters[] = {
    RootConstants{ .Register = 0, .SizeBytes = 7 * 4 },
    RootConstantBuffer{ .Register = 1 },
  };

  ComPtr<ID3D12RootSignature> root_signature = render_device->CreateRootSignature( {
      .RootParameters = root_parameters,
      .StaticSamplers = static_sampler_desc,
      .DebugName      = "Spot Light Deferred Root Signature",
  } );
  if ( not root_signature ) return false;

  CD3DX12_DEPTH_STENCIL_DESC depth_stencil_desc{ D3D12_DEFAULT };
  depth_stencil_desc.DepthEnable    = TRUE;
  depth_stencil_desc.DepthFunc      = D3D12_COMPARISON_FUNC_GREATER_EQUAL;
  depth_stencil_desc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;

  CD3DX12_BLEND_DESC blend_desc{ D3D12_DEFAULT };
  blend_desc.RenderTarget[0] = {
    .BlendEnable           = TRUE,
    .LogicOpEnable         = FALSE,
    .SrcBlend              = D3D12_BLEND_ONE,
    .DestBlend             = D3D12_BLEND_ONE,
    .BlendOp               = D3D12_BLEND_OP_ADD,
    .SrcBlendAlpha         = D3D12_BLEND_ONE,
    .DestBlendAlpha        = D3D12_BLEND_ONE,
    .BlendOpAlpha          = D3D12_BLEND_OP_ADD,
    .LogicOp               = D3D12_LOGIC_OP_NOOP,
    .RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL,
  };

  ComPtr<ID3D12PipelineState> pipeline = render_device->CreateGraphicsPipeline( {
      .RootSignature    = root_signature.Get(),
      .RTVFormats       = { &rt_format, 1 },
      .RasterizerDesc   = Rasterizer{ .CullMode = Rasterizer::CullMode::kFront },
      .DepthStencilDesc = depth_stencil_desc,
      .BlendDesc        = blend_desc,
      .AmpShaderName    = "SpotLightingAS.cso",
      .MeshShaderName   = "SpotLightingMS.cso",
      .PixelShaderName  = "SpotLightingPS.cso",
      .DSVFormat        = depth_format,
      .DebugName        = "Spot Light Deferred Pipeline",
  } );
  if ( not pipeline ) return false;

  out->RootSignature = std::move( root_signature );
  out->Pipeline      = std::move( pipeline );

  return true;
}

FrameGraphResource Ember::RenderPass::SpotLightDeferred::Execute(
    FrameGraph*                 frame_graph,
    FrameGraphBlackboard const& bb,
    GBuffer::Data const&        gbuffer,
    FrameGraphResource const    render_target ) const
{
  auto const  spot_light_count = bb.get<LightManager::GpuRepr>().SpotLightInfo.TotalLightCount;
  auto const& root_sig         = RootSignature;
  auto const& pipeline         = Pipeline;

  auto const& [constants_buf]  = bb.get<FrameConstants>();

  MergeData const& result      = frame_graph->addCallbackPass(
      "SpotLight Pass",
      [&]( FrameGraph::Builder& builder, MergeData& data )
      {
        for ( uint32_t i = 0; i < GBuffer::kGBufferCount; i++ )
        {
          data.GBuffer[i] = builder.read( gbuffer.GBuffer[i], FG::ShaderRead{} );
        }
        data.RenderTarget = builder.write( render_target, FG::Attachment{ .Index = 0, .ForceSrgb = true } );
        data.DepthStencil = builder.read( gbuffer.DepthStencil, FG::DepthStencilRead{} );
      },
      [=]( MergeData const& data, FrameGraphPassResources& resources, FG::Context const* context )
      {
        ZoneScopedN( "SpotLight Pass" );

        FG::Context::FrameData const& frame_data = context->GetFrameData();
        CommandList*                  cmd        = frame_data.CommandList;
        PIXScopedEvent( cmd->Get(), PIX_COLOR_DEFAULT, "SpotLight Pass" );

        SRVHandle gbuffer_handles[GBuffer::kGBufferCount];
        for ( uint32_t i = 0; i < GBuffer::kGBufferCount; i++ )
        {
          gbuffer_handles[i] = resources.get<FG::Texture>( data.GBuffer[i] ).GetSRVHandle();
        }

        cmd->SetGraphicsRootSignature( root_sig.Get() );
        cmd->SetPipelineState( pipeline.Get() );
        cmd->SetGraphicsRootConstants( 0, gbuffer_handles );
        cmd->SetGraphicsRootConstantBuffer( 1, constants_buf );
        cmd->DispatchMesh( { .X = ( spot_light_count + 31 ) / 32 } );
      } );
  return result.RenderTarget;
}

FrameGraphResource Ember::RenderPass::SpotLightDeferred::operator()(
    FrameGraph*                 frame_graph,
    FrameGraphBlackboard const& bb,
    GBuffer::Data const&        gbuffer,
    FrameGraphResource const    render_target ) const
{
  return Execute( frame_graph, bb, gbuffer, render_target );
}

bool Ember::RenderPass::ScreenSpaceLightDeferred::Create(
    ScreenSpaceLightDeferred* out, RenderDevice* render_device, DXGI_FORMAT const rt_format )
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
    CD3DX12_STATIC_SAMPLER_DESC{ 3, D3D12_FILTER_MIN_MAG_MIP_POINT },
  };

  D3D12_ROOT_PARAMETER1 root_parameters[] = {
    RootConstants{ .Register = 0, .SizeBytes = 7 * 4 },
    RootConstantBuffer{ .Register = 1 },
  };

  ComPtr<ID3D12RootSignature> root_signature = render_device->CreateRootSignature( {
      .RootParameters = root_parameters,
      .StaticSamplers = static_sampler_desc,
      .DebugName      = "Screen Space Light Deferred Root Signature",
  } );
  if ( not root_signature ) return false;

  CD3DX12_BLEND_DESC blend_desc{ D3D12_DEFAULT };
  blend_desc.RenderTarget[0] = {
    .BlendEnable           = TRUE,
    .LogicOpEnable         = FALSE,
    .SrcBlend              = D3D12_BLEND_ONE,
    .DestBlend             = D3D12_BLEND_ONE,
    .BlendOp               = D3D12_BLEND_OP_ADD,
    .SrcBlendAlpha         = D3D12_BLEND_ONE,
    .DestBlendAlpha        = D3D12_BLEND_ONE,
    .BlendOpAlpha          = D3D12_BLEND_OP_ADD,
    .LogicOp               = D3D12_LOGIC_OP_NOOP,
    .RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL,
  };

  ComPtr<ID3D12PipelineState> pipeline = render_device->CreateGraphicsPipeline( {
      .RootSignature    = root_signature.Get(),
      .RTVFormats       = { &rt_format, 1 },
      .BlendDesc        = blend_desc,
      .VertexShaderName = "ScreenSpaceTriangleVS.cso",
      .PixelShaderName  = "LightingPS.cso",
      .DebugName        = "Screen Space Light Deferred Pipeline",
  } );
  if ( not pipeline ) return false;

  out->RootSignature = std::move( root_signature );
  out->Pipeline      = std::move( pipeline );

  return true;
}

FrameGraphResource Ember::RenderPass::ScreenSpaceLightDeferred::Execute(
    FrameGraph*                 frame_graph,
    FrameGraphBlackboard const& bb,
    GBuffer::Data const&        gbuffer,
    FrameGraphResource const    render_target,
    FrameGraphResource const    ssao ) const
{
  auto const& root_sig        = RootSignature;
  auto const& pipeline        = Pipeline;
  auto const& [constants_buf] = bb.get<FrameConstants>();

  MergeData const& result     = frame_graph->addCallbackPass(
      "Screen Space Light Pass",
      [&]( FrameGraph::Builder& builder, MergeData& data )
      {
        for ( uint32_t i = 0; i < GBuffer::kGBufferCount; i++ )
        {
          data.GBuffer[i] = builder.read( gbuffer.GBuffer[i], FG::ShaderRead{} );
        }
        data.DepthStencil = builder.read( gbuffer.DepthStencil, FG::DepthStencilRead{} );

        if ( ssao.valid() ) data.SSAO = builder.read( ssao, FG::ShaderRead{} );

        data.RenderTarget = builder.write( render_target, FG::Attachment{ .Index = 0, .ForceSrgb = true } );
      },
      [=]( MergeData const& data, FrameGraphPassResources& resources, FG::Context const* context )
      {
        ZoneScopedN( "Screen Space Light Pass" );

        FG::Context::FrameData const& frame_data = context->GetFrameData();
        CommandList*                  cmd        = frame_data.CommandList;
        PIXScopedEvent( cmd->Get(), PIX_COLOR_DEFAULT, "Screen Space Light Pass" );

        SRVHandle gbuffer_handles[GBuffer::kGBufferCount + 1];
        for ( uint32_t i = 0; i < GBuffer::kGBufferCount; i++ )
        {
          gbuffer_handles[i] = resources.get<FG::Texture>( data.GBuffer[i] ).GetSRVHandle();
        }
        gbuffer_handles[GBuffer::kGBufferCount] =
            data.SSAO.valid() ? resources.get<FG::Texture>( data.SSAO ).GetSRVHandle() : SRVHandle{};

        cmd->SetGraphicsRootSignature( root_sig.Get() );
        cmd->SetPipelineState( pipeline.Get() );
        cmd->SetGraphicsRootConstants( 0, gbuffer_handles );
        cmd->SetGraphicsRootConstantBuffer( 1, constants_buf );
        cmd->DrawInstanced( 3, 1, 0, 0 );
      } );

  return result.RenderTarget;
}

FrameGraphResource Ember::RenderPass::ScreenSpaceLightDeferred::operator()(
    FrameGraph*                 frame_graph,
    FrameGraphBlackboard const& bb,
    GBuffer::Data const&        gbuffer,
    FrameGraphResource const    render_target,
    FrameGraphResource const    ssao ) const
{
  return Execute( frame_graph, bb, gbuffer, render_target, ssao );
}
