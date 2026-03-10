#include "SSAOBlurPass.hpp"

#include <Util/Profiling.hpp>
#include "FrameGraphHelper.hpp"
#include "fg/FrameGraph.hpp"

namespace
{

struct SSAOBlurData
{
  FrameGraphResource InputTexture;
  FrameGraphResource DepthTexture;
  FrameGraphResource OutputTexture;
};

struct PassHandles
{
  Ember::SRVHandle InputTexture;
  Ember::SRVHandle DepthTexture;
  Ember::UAVHandle OutputTexture;
};

} // namespace

bool Ember::RenderPass::ScreenSpaceAmbientOcclusionBlur::Create(
    ScreenSpaceAmbientOcclusionBlur* out, RenderDevice* render_device )
{
  D3D12_ROOT_PARAMETER1 root_parameters[] = {
    RootConstants{ .Register = 0, .SizeBytes = sizeof( PassHandles ) },
    RootConstantBuffer{ .Register = 1 },
  };

  D3D12_STATIC_SAMPLER_DESC static_sampler_desc[] = {
    CD3DX12_STATIC_SAMPLER_DESC{
                                0, D3D12_FILTER_MIN_MAG_MIP_LINEAR,
                                D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
                                D3D12_TEXTURE_ADDRESS_MODE_CLAMP, },
  };

  ComPtr<ID3D12RootSignature> root_signature = render_device->CreateRootSignature( {
      .RootParameters = root_parameters,
      .StaticSamplers = static_sampler_desc,
      .DebugName      = "SSAO Blur Root Signature",
  } );
  if ( not root_signature ) return false;

  ComPtr<ID3D12PipelineState> pipeline = render_device->CreateComputePipeline( {
      .RootSignature     = root_signature.Get(),
      .ComputeShaderName = "SSAOBlurCS.cso",
      .DebugName         = "SSAO Blur Pipeline",
  } );
  if ( not pipeline ) return false;

  out->RootSignature = std::move( root_signature );
  out->Pipeline      = std::move( pipeline );

  return true;
}

FrameGraphResource Ember::RenderPass::ScreenSpaceAmbientOcclusionBlur::Execute(
    FrameGraph*                 frame_graph,
    FrameGraphBlackboard const& bb,
    FrameGraphResource const    ssao_texture,
    FrameGraphResource const    depth_texture ) const
{
  auto const& root_sig        = RootSignature;
  auto const& pipeline        = Pipeline;
  auto const& backbuffer_info = bb.get<FG::BackbufferInfo>();
  auto const  width           = backbuffer_info.Width >> 1;
  auto const  height          = backbuffer_info.Height >> 1;

  auto const& [constants_buf] = bb.get<FrameConstants>();

  SSAOBlurData const& result  = frame_graph->addCallbackPass(
      "SSAO Blur Pass",
      [&]( FrameGraph::Builder& builder, SSAOBlurData& data )
      {
        data.InputTexture = builder.read( ssao_texture, FG::ShaderRead{ .PixelShaderUse = false } );
        data.DepthTexture = builder.read( depth_texture, FG::ShaderRead{ .PixelShaderUse = false } );

        FrameGraphResource const blurred_target = builder.create<FG::Texture>(
            "SSAO Blurred",
            {
                 .Format      = DXGI_FORMAT_R8_UNORM,
                 .Width       = width,
                 .Height      = height,
                 .MipLevels   = MipLevels::kBase,
                 .Type        = TextureType::kSampled,
                 .IsReadWrite = true,
            } );

        data.OutputTexture = builder.write( blurred_target, FG::ShaderWrite{} );
      },
      [=]( SSAOBlurData const& data, FrameGraphPassResources& resources, FG::Context const* context )
      {
        ZoneScopedN( "SSAO Blur Pass" );

        FG::Context::FrameData const& frame_data = context->GetFrameData();
        CommandList const*            cmd        = frame_data.CommandList;
        PIXScopedEvent( cmd->Get(), PIX_COLOR_DEFAULT, "SSAO Blur Pass" );

        PassHandles const pass_handles = {
          resources.get<FG::Texture>( data.InputTexture ).GetSRVHandle(),
          resources.get<FG::Texture>( data.DepthTexture ).GetSRVHandle(),
          resources.get<FG::Texture>( data.OutputTexture ).GetUAVHandle(),
        };

        cmd->SetComputeRootSignature( root_sig.Get() );
        cmd->SetPipelineState( pipeline.Get() );
        cmd->SetComputeRootConstants( 0, pass_handles );
        cmd->SetComputeRootConstantBuffer( 1, constants_buf );
        cmd->Dispatch( { .X = ( width + 7 ) / 8, .Y = ( height + 7 ) / 8 } );
      } );

  return result.OutputTexture;
}

FrameGraphResource Ember::RenderPass::ScreenSpaceAmbientOcclusionBlur::operator()(
    FrameGraph*                 frame_graph,
    FrameGraphBlackboard const& bb,
    FrameGraphResource const    ssao_texture,
    FrameGraphResource const    depth_texture ) const
{
  return Execute( frame_graph, bb, ssao_texture, depth_texture );
}
