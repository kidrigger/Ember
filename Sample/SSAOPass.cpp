#include "SSAOPass.hpp"

#include <Util/DataUtil.hpp>
#include <Util/Profiling.hpp>
#include <random>
#include "FrameGraphHelper.hpp"
#include "RenderPassCommon.hpp"
#include "fg/FrameGraph.hpp"

namespace
{

struct SSAOData
{
  FrameGraphResource Position;
  FrameGraphResource Normal;
  FrameGraphResource Depth;
  FrameGraphResource Kernel;
  FrameGraphResource Rotation;
  FrameGraphResource OutTexture;
};

struct PassHandles
{
  Ember::SRVHandle Position;
  Ember::SRVHandle Normal;
  Ember::SRVHandle Depth;
  Ember::SRVHandle Kernel;
  Ember::SRVHandle Rotation;
  Ember::UAVHandle OutTexture;
};

} // namespace

bool Ember::RenderPass::ScreenSpaceAmbientOcclusion::Create(
    ScreenSpaceAmbientOcclusion* out, RenderDevice* render_device )
{
  D3D12_STATIC_SAMPLER_DESC static_sampler_desc[] = {
    CD3DX12_STATIC_SAMPLER_DESC{ 0,
                                D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
                                D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP },
  };

  D3D12_ROOT_PARAMETER1 root_parameters[] = {
    RootConstants{ .Register = 0, .SizeBytes = sizeof( PassHandles ) }, // Position + Normal SRV handles
    RootConstantBuffer{ .Register = 1 }, // Frame constants (camera matrices, etc.)
  };

  ComPtr<ID3D12RootSignature> root_signature = render_device->CreateRootSignature( {
      .RootParameters = root_parameters,
      .StaticSamplers = static_sampler_desc,
      .DebugName      = "SSAO Root Signature",
  } );
  if ( not root_signature ) return false;

  ComPtr<ID3D12PipelineState> pipeline = render_device->CreateComputePipeline( {
      .RootSignature     = root_signature.Get(),
      .ComputeShaderName = "ScreenSpaceAmbientOcclusionCS.cso",
      .DebugName         = "SSAO Pipeline",
  } );
  if ( not pipeline ) return false;

  std::default_random_engine     generator;
  std::uniform_real_distribution random_norm( 0.0f, 1.0f );

  std::vector<DirectX::XMFLOAT3> ssao_kernel( 64 );
  for ( int i = 0; auto& sample_dir : ssao_kernel )
  {
    float const scale = std::lerp( 0.1f, 1.0f, ( float )( i * i ) / 4096.0f );

    float       x, y, z;
    do
    {
      x = random_norm( generator ) * 2.0f - 1.0f;
      y = random_norm( generator ) * 2.0f - 1.0f;
      z = random_norm( generator ) * 2.0f - 1.0f;
    }
    while ( x * x + y * y + z * z > 1.0f );

    sample_dir = { scale * x, scale * y, scale * z };

    i++;
  }

  std::vector<DirectX::XMFLOAT4> ssao_rotation( 64 );
  for ( auto& rotation : ssao_rotation )
  {
    float x, y, z, u, v, w;
    do
    {
      x = random_norm( generator ) * 2.0f - 1.0f;
      y = random_norm( generator ) * 2.0f - 1.0f;
      z = x * x + y * y;
    }
    while ( z > 1 );
    do
    {
      u = random_norm( generator ) * 2.0f - 1.0f;
      v = random_norm( generator ) * 2.0f - 1.0f;
      w = u * u + v * v;
    }
    while ( w > 1 );
    float const s = sqrt( ( 1 - z ) / w );
    rotation      = DirectX::XMFLOAT4{ x, y, s * u, s * v };
  }

  Buffer kernel = render_device->CreateRawStorageBuffer( ByteSizeOf( ssao_kernel ) );
  kernel.Write( 0, ByteSizeOf( ssao_kernel ), DataOf( ssao_kernel ) );

  Buffer rotation = render_device->CreateRawStorageBuffer( ByteSizeOf( ssao_rotation ) );
  rotation.Write( 0, ByteSizeOf( ssao_rotation ), DataOf( ssao_rotation ) );

  out->RootSignature = std::move( root_signature );
  out->Pipeline      = std::move( pipeline );
  out->Kernel        = std::move( kernel );
  out->Rotation      = std::move( rotation );

  return true;
}

FrameGraphResource Ember::RenderPass::ScreenSpaceAmbientOcclusion::Execute(
    FrameGraph*                 frame_graph,
    FrameGraphBlackboard const& bb,
    GBuffer::Data const&        gbuffer,
    FrameGraphResource const    depth_buffer ) const
{
  auto const& root_sig            = RootSignature;
  auto const& pipeline            = Pipeline;
  auto const& [constants_buf]     = bb.get<FrameConstants>();

  auto const&     backbuffer_info = bb.get<FG::BackbufferInfo>();
  auto const      width           = backbuffer_info.Width >> 1;
  auto const      height          = backbuffer_info.Height >> 1;

  auto const      kernel_res      = frame_graph->import( "SSAO Kernel", FG::Buffer::Desc{}, FG::Buffer{ Kernel } );
  auto const      rotation_res    = frame_graph->import( "SSAO Rotation", FG::Buffer::Desc{}, FG::Buffer{ Rotation } );

  SSAOData const& result          = frame_graph->addCallbackPass(
      "SSAO Pass",
      [&]( FrameGraph::Builder& builder, SSAOData& data )
      {
        data.Position                          = builder.read( gbuffer.GBuffer[GBuffer::kPosition], FG::ShaderRead{} );
        data.Normal                            = builder.read( gbuffer.GBuffer[GBuffer::kNormal], FG::ShaderRead{} );
        data.Depth                             = builder.read( depth_buffer, FG::ShaderRead{} );

        data.Kernel                            = builder.read( kernel_res, FG::ShaderRead{} );
        data.Rotation                          = builder.read( rotation_res, FG::ShaderRead{} );

        FrameGraphResource const render_target = builder.create<FG::Texture>(
            "SSAO Render Target",
            {
                         .Format    = DXGI_FORMAT_R16_UNORM,
                         .Width     = width,
                         .Height    = height,
                         .MipLevels = MipLevels::kBase,
                         .Usage     = TextureUsage::kReadWrite,
                         .InitState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            } );

        data.OutTexture = builder.write( render_target, FG::ShaderWrite{} );
      },
      [=]( SSAOData const& data, FrameGraphPassResources& resources, FG::Context const* context )
      {
        ZoneScopedN( "SSAO Pass" );

        FG::Context::FrameData const& frame_data = context->GetFrameData();
        CommandList const*            cmd        = frame_data.CommandList;
        PIXScopedEvent( cmd->Get(), PIX_COLOR_DEFAULT, "SSAO Pass" );

        PassHandles const pass_handles = {
          resources.get<FG::Texture>( data.Position ).GetSRVHandle(),
          resources.get<FG::Texture>( data.Normal ).GetSRVHandle(),
          resources.get<FG::Texture>( data.Depth ).GetSRVHandle(),
          resources.get<FG::Buffer>( data.Kernel ).InnerBuffer.GetSRVHandle(),
          resources.get<FG::Buffer>( data.Rotation ).InnerBuffer.GetSRVHandle(),
          resources.get<FG::Texture>( data.OutTexture ).GetUAVHandle(),
        };

        cmd->SetComputeRootSignature( root_sig.Get() );
        cmd->SetPipelineState( pipeline.Get() );
        cmd->SetComputeRootConstants( 0, pass_handles );
        cmd->SetComputeRootConstantBuffer( 1, constants_buf );
        cmd->Dispatch( { .X = ( width + 7 ) / 8, .Y = ( height + 7 ) / 8 } );
      } );

  return result.OutTexture;
}

FrameGraphResource Ember::RenderPass::ScreenSpaceAmbientOcclusion::operator()(
    FrameGraph*                 frame_graph,
    FrameGraphBlackboard const& bb,
    GBuffer::Data const&        gbuffer,
    FrameGraphResource const    depth_buffer ) const
{
  return Execute( frame_graph, bb, gbuffer, depth_buffer );
}
