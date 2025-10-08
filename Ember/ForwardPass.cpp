#include "ForwardPass.hpp"

#include "Environment.hpp"
#include "RenderDevice.hpp"
#include "Scene.hpp"
#include "Util/DataUtil.hpp"
#include "Util/HelperUtils.hpp"

bool Ember::RenderPass::Forward::Create( Forward* out, RenderDevice* render_device )
{
  ComPtr<ID3DBlob> amp_shader_blob;
  ERR_FAIL_RET_F( D3DReadFileToBlob( L"TriangleAS.cso", &amp_shader_blob ) );
  ComPtr<ID3DBlob> mesh_shader_blob;
  ERR_FAIL_RET_F( D3DReadFileToBlob( L"TriangleMS.cso", &mesh_shader_blob ) );
  ComPtr<ID3DBlob> pixel_shader_blob;
  ERR_FAIL_RET_F( D3DReadFileToBlob( L"TrianglePS.cso", &pixel_shader_blob ) );
  ComPtr<ID3DBlob> alpha_tested_pixel_shader_blob;
  ERR_FAIL_RET_F( D3DReadFileToBlob( L"TriangleAlphaTestPS.cso", &alpha_tested_pixel_shader_blob ) );
  ComPtr<ID3DBlob> alpha_blended_pixel_shader_blob;
  ERR_FAIL_RET_F( D3DReadFileToBlob( L"TriangleAlphaBlendPS.cso", &alpha_blended_pixel_shader_blob ) );

  ComPtr<ID3D12Device2>       device                 = render_device->GetDevice();

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

  CD3DX12_ROOT_PARAMETER1 root_parameters[3];
  root_parameters[0].InitAsConstants( sizeof( DrawList::Info ) / 4, 0 );
  root_parameters[1].InitAsConstants( sizeof( PerFrameConstants ) / 4, 1 );
  root_parameters[2].InitAsConstants( sizeof( Environment::GpuRepr ) / 4, 2 );

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

  ERR_FAIL_RET_F( device->CreateRootSignature(
      0,
      root_signature_blob->GetBufferPointer(),
      root_signature_blob->GetBufferSize(),
      IID_PPV_ARGS( out->RootSignature.ReleaseAndGetAddressOf() ) ) );

  D3D12_RT_FORMAT_ARRAY rtv_formats{
    .RTFormats        = { DXGI_FORMAT_R8G8B8A8_UNORM },
    .NumRenderTargets = 1,
  };

  CD3DX12_RASTERIZER_DESC2 rasterizer_desc{ D3D12_DEFAULT };
  rasterizer_desc.FrontCounterClockwise = TRUE;
  rasterizer_desc.CullMode              = D3D12_CULL_MODE_BACK;

  struct MainPipelineStream
  {
    CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE        RootSignature;
    CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY    PrimitiveTopologyType;
    CD3DX12_PIPELINE_STATE_STREAM_AS                    AS;
    CD3DX12_PIPELINE_STATE_STREAM_MS                    MS;
    CD3DX12_PIPELINE_STATE_STREAM_PS                    PS;
    CD3DX12_PIPELINE_STATE_STREAM_BLEND_DESC            Blending;
    CD3DX12_PIPELINE_STATE_STREAM_RASTERIZER2           Rasterizer;
    CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS RTVFormats;
    CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL_FORMAT  DSVFormat;
  };

  MainPipelineStream pipeline_stream = {
    .RootSignature         = out->RootSignature.Get(),
    .PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
    .AS                    = CD3DX12_SHADER_BYTECODE( amp_shader_blob.Get() ),
    .MS                    = CD3DX12_SHADER_BYTECODE( mesh_shader_blob.Get() ),
    .PS                    = CD3DX12_SHADER_BYTECODE( pixel_shader_blob.Get() ),
    .Blending              = CD3DX12_BLEND_DESC{ D3D12_DEFAULT },
    .Rasterizer            = rasterizer_desc,
    .RTVFormats            = rtv_formats,
    .DSVFormat             = DXGI_FORMAT_D32_FLOAT,
  };

  D3D12_PIPELINE_STATE_STREAM_DESC const pipeline_state_stream_desc = {
    .SizeInBytes                   = sizeof pipeline_stream,
    .pPipelineStateSubobjectStream = &pipeline_stream,
  };

  ERR_FAIL_RET_F( device->CreatePipelineState(
      &pipeline_state_stream_desc, IID_PPV_ARGS( out->OpaquePipeline.ReleaseAndGetAddressOf() ) ) );

  pipeline_stream.PS = CD3DX12_SHADER_BYTECODE( alpha_tested_pixel_shader_blob.Get() );
  ERR_FAIL_RET_F( device->CreatePipelineState(
      &pipeline_state_stream_desc, IID_PPV_ARGS( out->AlphaTestedPipeline.ReleaseAndGetAddressOf() ) ) );

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

  pipeline_stream.PS       = CD3DX12_SHADER_BYTECODE( alpha_blended_pixel_shader_blob.Get() );
  pipeline_stream.Blending = blend_desc;
  ERR_FAIL_RET_F( device->CreatePipelineState(
      &pipeline_state_stream_desc, IID_PPV_ARGS( out->AlphaBlendedPipeline.ReleaseAndGetAddressOf() ) ) );

  return true;
}
