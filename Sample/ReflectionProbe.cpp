#include "ReflectionProbe.hpp"

#include "Environment.hpp"
#include "FrameGraphHelper.hpp"
#include "fg/Blackboard.hpp"
#include "fg/FrameGraph.hpp"

#include <Util/Profiling.hpp>

#include "TextureLoader.hpp"

Ember::Proto::ReflectionProbe::ReflectionProbe(
    ComPtr<ID3D12PipelineState> pipeline, ComPtr<ID3D12RootSignature> root_signature, Probe const& probe )
  : m_Pipeline{ std::move( pipeline ) }, m_RootSignature{ std::move( root_signature ) }, ProbeInfo{ probe }
{}

bool Ember::Proto::ReflectionProbe::Create(
    ReflectionProbe* out, RenderDevice* render_device, DirectX::XMFLOAT3 position, float radius )
{
  ComPtr<ID3DBlob> amp_shader_blob;
  ERR_FAIL_RET_F( D3DReadFileToBlob( L"ReflectionProbeAS.cso", &amp_shader_blob ) );
  ComPtr<ID3DBlob> mesh_shader_blob;
  ERR_FAIL_RET_F( D3DReadFileToBlob( L"ReflectionProbeMS.cso", &mesh_shader_blob ) );
  ComPtr<ID3DBlob> pixel_shader_blob;
  ERR_FAIL_RET_F( D3DReadFileToBlob( L"ReflectionProbePS.cso", &pixel_shader_blob ) );

  ID3D12Device2*              device                 = render_device->GetDevice();

  D3D_ROOT_SIGNATURE_VERSION  root_signature_version = render_device->FetchHighestRootSignatureVersion();

  CD3DX12_STATIC_SAMPLER_DESC static_sampler_desc[]  = {
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

  D3D12_ROOT_SIGNATURE_FLAGS const root_signature_flags = D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED |
                                                          D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED |
                                                          D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS |
                                                          D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
                                                          D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
                                                          D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS;

  CD3DX12_ROOT_PARAMETER1 root_parameters[4];
  root_parameters[0].InitAsConstants( sizeof( DrawList::PerBatch ) / 4, 0 );
  root_parameters[1].InitAsConstants( sizeof( PerFrameConstants ) / 4, 1 );
  root_parameters[2].InitAsConstants( sizeof( Environment::GpuRepr ) / 4, 2 );
  root_parameters[3].InitAsConstants( sizeof( Probe ) / 4, 3 );

  CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC root_signature_desc;
  root_signature_desc.Init_1_1(
      CountOf( root_parameters ),
      DataOf( root_parameters ),
      CountOf( static_sampler_desc ),
      DataOf( static_sampler_desc ),
      root_signature_flags );

  ComPtr<ID3DBlob> root_signature_blob;
  ComPtr<ID3DBlob> error_blob;
  ERR_FAIL_RET_F( D3DX12SerializeVersionedRootSignature(
      &root_signature_desc, root_signature_version, &root_signature_blob, &error_blob ) );

  ComPtr<ID3D12RootSignature> root_signature;
  ERR_FAIL_RET_F( device->CreateRootSignature(
      0,
      root_signature_blob->GetBufferPointer(),
      root_signature_blob->GetBufferSize(),
      IID_PPV_ARGS( &root_signature ) ) );

  D3D12_RT_FORMAT_ARRAY rtv_formats{
    .RTFormats        = { kRenderTargetFormat },
    .NumRenderTargets = 1,
  };

  CD3DX12_RASTERIZER_DESC2 rasterizer_desc{ D3D12_DEFAULT };
  rasterizer_desc.FrontCounterClockwise = FALSE;
  rasterizer_desc.CullMode              = D3D12_CULL_MODE_BACK;

  CD3DX12_DEPTH_STENCIL_DESC depth_stencil_desc{ D3D12_DEFAULT };
  depth_stencil_desc.DepthFunc = D3D12_COMPARISON_FUNC_LESS;

  struct PipelineStream
  {
    CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE        RootSignature;
    CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY    PrimitiveTopologyType;
    CD3DX12_PIPELINE_STATE_STREAM_AS                    AS;
    CD3DX12_PIPELINE_STATE_STREAM_MS                    MS;
    CD3DX12_PIPELINE_STATE_STREAM_PS                    PS;
    CD3DX12_PIPELINE_STATE_STREAM_RASTERIZER2           Rasterizer;
    CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL         DepthStencil;
    CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS RTVFormats;
    CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL_FORMAT  DSVFormat;
  };

  PipelineStream pipeline_stream = {
    .RootSignature         = root_signature.Get(),
    .PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
    .AS                    = CD3DX12_SHADER_BYTECODE( amp_shader_blob.Get() ),
    .MS                    = CD3DX12_SHADER_BYTECODE( mesh_shader_blob.Get() ),
    .PS                    = CD3DX12_SHADER_BYTECODE( pixel_shader_blob.Get() ),
    .Rasterizer            = rasterizer_desc,
    .DepthStencil          = depth_stencil_desc,
    .RTVFormats            = rtv_formats,
    .DSVFormat             = kDepthFormat,
  };

  D3D12_PIPELINE_STATE_STREAM_DESC const pipeline_state_stream_desc = {
    .SizeInBytes                   = sizeof pipeline_stream,
    .pPipelineStateSubobjectStream = &pipeline_stream,
  };

  ComPtr<ID3D12PipelineState> pipeline;
  ERR_FAIL_RET_F( device->CreatePipelineState( &pipeline_state_stream_desc, IID_PPV_ARGS( &pipeline ) ) );

  new ( out ) ReflectionProbe{
    std::move( pipeline ),
    std::move( root_signature ),
    { position.x, position.y, position.z, radius },
  };

  return true;
}

FrameGraphResource Ember::Proto::ReflectionProbe::Execute(
    FrameGraph* frame_graph, FrameGraphBlackboard const& blackboard, TextureLoader* loader ) const
{
  auto const& constants  = blackboard.get<PerFrameConstants>();
  auto const& env        = blackboard.get<Environment::GpuRepr>();
  auto const& batch      = blackboard.get<DrawList::Batches>().Opaque();
  auto const  root_sig   = m_RootSignature;
  auto const  pipeline   = m_Pipeline;
  auto const& probe_info = ProbeInfo;

  auto        probe      = frame_graph->addCallbackPass(
      "Reflection Probe Capture",
      [&]( FrameGraph::Builder& builder, std::pair<FrameGraphResource, FrameGraphResource>& probe )
      {
        probe.first = builder.create<FG::Texture>(
            "Reflection Probe",
            {
                            .Format    = DXGI_FORMAT_R11G11B10_FLOAT,
                            .Width     = kSide,
                            .Height    = kSide,
                            .MipLevels = MipLevels::kAuto,
                            .ArraySize = 1,
                            .Usage     = TextureUsage::kRenderTarget,
                            .Dim       = TextureDim::kCube,
                            .InitState = D3D12_RESOURCE_STATE_RENDER_TARGET,
            } );
        probe.second = builder.create<FG::Texture>(
            "Reflection Probe",
            {
                            .Format    = kDepthFormat,
                            .Width     = kSide,
                            .Height    = kSide,
                            .MipLevels = MipLevels::kAuto,
                            .ArraySize = 1,
                            .Usage     = TextureUsage::kDepthStencil,
                            .Dim       = TextureDim::kCube,
                            .InitState = D3D12_RESOURCE_STATE_DEPTH_WRITE,
            } );

        probe.first = builder.write( probe.first, FG::Attachment{ .Index = 0, .LoadOp = FG::LoadOperation::kClear } );
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
        cmd->SetGraphicsRootConstants( 1, constants );
        cmd->SetGraphicsRootConstants( 2, env );
        cmd->SetGraphicsRootConstants( 3, probe_info );
        cmd->DispatchMesh( { .X = batch.CommandsCount } );
      } );

  return frame_graph->addCallbackPass(
      "Reflection Probe Mipmap",
      [&]( FrameGraph::Builder& builder, FrameGraphResource& base_probe )
      { base_probe = builder.write( probe.first, FG::CopyDst{} ); },
      [=]( FrameGraphResource const& base_probe, FrameGraphPassResources& res, FG::Context const* context )
      {
        // TODO: This will be super expensive without some 'serious' culling.
        ZoneScopedN( "Reflection Probe Mipmap" );

        FG::Context::FrameData const& frame_data = context->GetFrameData();
        CommandList*                  cmd        = frame_data.CommandList;
        PIXScopedEvent( cmd->Get(), PIX_COLOR_DEFAULT, "Reflection Probe Mipmap" );

        FG::Texture* tex    = &res.get<FG::Texture>( base_probe );

        bool         result = loader->TryGenerateMipMapCube( cmd, tex );
        ASSERT( result );
      } );
}

FrameGraphResource Ember::Proto::ReflectionProbe::operator()(
    FrameGraph* frame_graph, FrameGraphBlackboard const& blackboard, TextureLoader* loader ) const
{
  return Execute( frame_graph, blackboard, loader );
}
