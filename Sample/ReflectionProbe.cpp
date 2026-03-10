#include "ReflectionProbe.hpp"

#include "Environment.hpp"
#include "FrameGraphHelper.hpp"
#include "fg/Blackboard.hpp"
#include "fg/FrameGraph.hpp"

#include <Util/Profiling.hpp>

#include "TextureLoader.hpp"

Ember::Proto::ReflectionProbe::ReflectionProbe(
    MipMapGenerator*            mip_map_generator,
    ComPtr<ID3D12PipelineState> pipeline,
    ComPtr<ID3D12RootSignature> root_signature,
    Probe const&                probe )
  : m_MipMapGenerator{ mip_map_generator }
  , m_Pipeline{ std::move( pipeline ) }
  , m_RootSignature{ std::move( root_signature ) }
  , ProbeInfo{ probe }
{}

bool Ember::Proto::ReflectionProbe::Create(
    ReflectionProbe*  out,
    RenderDevice*     render_device,
    MipMapGenerator*  mip_map_generator,
    DirectX::XMFLOAT3 position,
    float             radius )
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
    RootConstants{ .Register = 2, .SizeBytes = sizeof( Probe ) },
  };

  ComPtr<ID3D12RootSignature> root_signature = render_device->CreateRootSignature( {
      .RootParameters = root_parameters,
      .StaticSamplers = static_sampler_desc,
      .DebugName      = "Reflection Probe Root Signature",
  } );
  if ( not root_signature ) return false;

  CD3DX12_DEPTH_STENCIL_DESC depth_stencil_desc{ D3D12_DEFAULT };
  depth_stencil_desc.DepthFunc         = D3D12_COMPARISON_FUNC_LESS;

  ComPtr<ID3D12PipelineState> pipeline = render_device->CreateGraphicsPipeline( {
      .RootSignature    = root_signature.Get(),
      .RTVFormats       = { &kRenderTargetFormat, 1 },
      .RasterizerDesc   = Rasterizer{ .FrontFace = Rasterizer::FrontFace::kClockwise },
      .DepthStencilDesc = depth_stencil_desc,
      .AmpShaderName    = "ReflectionProbeAS.cso",
      .MeshShaderName   = "ReflectionProbeMS.cso",
      .PixelShaderName  = "ReflectionProbePS.cso",
      .DSVFormat        = kDepthFormat,
      .DebugName        = "Reflection Probe Pipeline",
  } );
  if ( not pipeline ) return false;

  new ( out ) ReflectionProbe{
    mip_map_generator,
    std::move( pipeline ),
    std::move( root_signature ),
    { position.x, position.y, position.z, radius },
  };

  return true;
}

FrameGraphResource Ember::Proto::ReflectionProbe::Execute(
    FrameGraph* frame_graph, FrameGraphBlackboard const& blackboard ) const
{
  auto const& [constants_buf] = blackboard.get<FrameConstants>();
  auto const& batch           = blackboard.get<DrawList::Batches>().Opaque();
  auto const  root_sig        = m_RootSignature;
  auto const  pipeline        = m_Pipeline;
  auto const& probe_info      = ProbeInfo;

  auto const  probe           = frame_graph->addCallbackPass(
      "Reflection Probe Capture",
      [&]( FrameGraph::Builder& builder, std::pair<FrameGraphResource, FrameGraphResource>& probe )
      {
        probe.first = builder.create<FG::Texture>(
            "Reflection Probe",
            {
                           .Format    = kRenderTargetFormat,
                           .Width     = kSide,
                           .Height    = kSide,
                           .MipLevels = MipLevels::kAuto,
                           .ArraySize = 1,
                           .Type      = TextureType::kRenderTarget,
                           .Dim       = TextureDim::kCube,
                           .InitState = D3D12_RESOURCE_STATE_RENDER_TARGET,
            } );
        probe.second = builder.create<FG::Texture>(
            "Reflection Probe Depth",
            {
                           .Format    = kDepthFormat,
                           .Width     = kSide,
                           .Height    = kSide,
                           .MipLevels = MipLevels::kAuto,
                           .ArraySize = 1,
                           .Type      = TextureType::kDepthStencil,
                           .Dim       = TextureDim::kCube,
                           .InitState = D3D12_RESOURCE_STATE_DEPTH_WRITE,
            } );

        probe.first  = builder.write( probe.first, FG::Attachment{ .Index = 0, .LoadOp = FG::LoadOperation::kClear } );
        probe.second = builder.write( probe.second, FG::DepthStencil{ .LoadOp = FG::LoadOperation::kClear } );
      },
      [=]( std::pair<FrameGraphResource, FrameGraphResource> const&,
           FrameGraphPassResources&,
           FG::Context const* context )
      {
        // TODO: This will be super expensive without some 'serious' culling.
        ZoneScopedN( "Reflection Probe Capture" );

        FG::Context::FrameData const& frame_data = context->GetFrameData();
        CommandList const*            cmd        = frame_data.CommandList;
        PIXScopedEvent( cmd->Get(), PIX_COLOR_DEFAULT, "Reflection Probe Capture" );

        cmd->SetGraphicsRootSignature( root_sig.Get() );
        cmd->SetPipelineState( pipeline.Get() );
        cmd->SetGraphicsRootConstants( 0, batch );
        cmd->SetGraphicsRootConstantBuffer( 1, constants_buf );
        cmd->SetGraphicsRootConstants( 2, probe_info );
        cmd->DispatchMesh( { .X = batch.CommandsCount } );
      } );

  return frame_graph->addCallbackPass(
      "Reflection Probe Mipmap",
      [&]( FrameGraph::Builder& builder, FrameGraphResource& base_probe )
      { base_probe = builder.write( probe.first, FG::CopyDst{} ); },
      [=]( FrameGraphResource const& base_probe, FrameGraphPassResources& res, FG::Context const* context )
      {
        ZoneScopedN( "Reflection Probe Mipmap" );

        FG::Context::FrameData const& frame_data = context->GetFrameData();
        CommandList*                  cmd        = frame_data.CommandList;
        PIXScopedEvent( cmd->Get(), PIX_COLOR_DEFAULT, "Reflection Probe Mipmap" );

        FG::Texture* tex    = &res.get<FG::Texture>( base_probe );

        bool         result = m_MipMapGenerator->TryGenerateMipMapCube( cmd, tex );

        ASSERT( result );
      } );
}

FrameGraphResource Ember::Proto::ReflectionProbe::operator()(
    FrameGraph* frame_graph, FrameGraphBlackboard const& blackboard ) const
{
  return Execute( frame_graph, blackboard );
}
