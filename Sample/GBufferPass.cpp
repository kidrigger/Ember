#include "GBufferPass.hpp"

#include <Util/DataUtil.hpp>
#include <Util/Profiling.hpp>
#include "Environment.hpp"
#include "FrameGraphHelper.hpp"
#include "RenderPassCommon.hpp"
#include "fg/Blackboard.hpp"
#include "fg/FrameGraph.hpp"

bool Ember::RenderPass::GBuffer::Create( GBuffer* out, RenderDevice* render_device, DXGI_FORMAT const depth_format )
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
  };

  D3D12_ROOT_PARAMETER1 root_parameters[] = {
    RootConstants{ .Register = 0, .SizeBytes = sizeof( DrawList::PerBatch ) },
    RootConstantBuffer{ .Register = 1 },
    RootConstants{ .Register = 2, .SizeBytes = sizeof( Environment::GpuRepr ) },
  };

  ComPtr<ID3D12RootSignature> root_signature = render_device->CreateRootSignature( {
      .RootParameters = root_parameters,
      .StaticSamplers = static_sampler_desc,
      .DebugName      = "GBuffer Root Signature",
  } );
  if ( not root_signature ) return false;

  CD3DX12_DEPTH_STENCIL_DESC depth_stencil_desc{ D3D12_DEFAULT };
  depth_stencil_desc.DepthFunc      = D3D12_COMPARISON_FUNC_EQUAL;
  depth_stencil_desc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;

  std::vector const           gbuffer_formats( kGBufferFormats, kGBufferFormats + kGBufferCount );

  ComPtr<ID3D12PipelineState> pipeline = render_device->CreateGraphicsPipeline( {
      .RootSignature    = root_signature.Get(),
      .RTVFormats       = std::move( gbuffer_formats ),
      .DepthStencilDesc = depth_stencil_desc,
      .AmpShaderName    = "TriangleAS.cso",
      .MeshShaderName   = "TriangleMS.cso",
      .PixelShaderName  = "GBufferPS.cso",
      .DSVFormat        = depth_format,
      .DebugName        = "GBuffer Pipeline",
  } );
  if ( not pipeline ) return false;

  out->RootSignature = std::move( root_signature );
  out->Pipeline      = std::move( pipeline );

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
          .Usage     = TextureUsage::kRenderTarget,
          .InitState = D3D12_RESOURCE_STATE_RENDER_TARGET,
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
        CommandList*                  cmd        = frame_data.CommandList;
        PIXScopedEvent( cmd->Get(), PIX_COLOR_DEFAULT, "GBuffer Pass" );

        auto const& [constants_buf] = bb->get<FrameConstants>();
        auto const& env             = bb->get<Environment::GpuRepr>();
        auto const& draw_list       = bb->get<DrawList::Batches>();

        auto const  batch           = draw_list.Opaque();

        cmd->SetGraphicsRootSignature( self->RootSignature.Get() );
        cmd->SetPipelineState( self->Pipeline.Get() );
        cmd->SetGraphicsRootConstants( 0, batch );
        cmd->SetGraphicsRootConstantBuffer( 1, constants_buf );
        cmd->SetGraphicsRootConstants( 2, env );
        cmd->DispatchMesh( { .X = batch.CommandsCount } );
      } );
}

Ember::RenderPass::GBuffer::Data Ember::RenderPass::GBuffer::operator()(
    FrameGraph* frame_graph, FrameGraphBlackboard const& bb, FrameGraphResource const depth_stencil )
{
  return Execute( frame_graph, bb, depth_stencil );
}
