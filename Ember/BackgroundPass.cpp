#include "BackgroundPass.hpp"

#include "RenderDevice.hpp"
#include "Util/DataUtil.hpp"
#include "Util/HelperUtils.hpp"

bool Ember::RenderPass::Background::Create( Background* out, RenderDevice* render_device )
{
  ComPtr<ID3DBlob> bg_mesh_shader_blob;
  ERR_FAIL_RET_F( D3DReadFileToBlob( L"BackgroundMS.cso", &bg_mesh_shader_blob ) );
  ComPtr<ID3DBlob> bg_pixel_shader_blob;
  ERR_FAIL_RET_F( D3DReadFileToBlob( L"BackgroundPS.cso", &bg_pixel_shader_blob ) );

  ComPtr<ID3D12Device2>      device                 = render_device->GetDevice();

  D3D_ROOT_SIGNATURE_VERSION root_signature_version = render_device->FetchHighestRootSignatureVersion();

  CD3DX12_ROOT_PARAMETER1    root_parameters[1];
  root_parameters[0].InitAsConstants( 2, 0 );

  CD3DX12_STATIC_SAMPLER_DESC      static_sampler_desc  = CD3DX12_STATIC_SAMPLER_DESC{ 0 };

  D3D12_ROOT_SIGNATURE_FLAGS const root_signature_flags = D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED |
                                                          D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED |
                                                          D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS |
                                                          D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
                                                          D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
                                                          D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS;

  CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC root_signature_desc;
  root_signature_desc.Init_1_1(
      CountOf( root_parameters ), DataOf( root_parameters ), 1, &static_sampler_desc, root_signature_flags );

  ComPtr<ID3DBlob> root_signature_blob;
  ComPtr<ID3DBlob> error_blob;
  ERR_FAIL_RET_F( D3DX12SerializeVersionedRootSignature(
      &root_signature_desc, root_signature_version, root_signature_blob.ReleaseAndGetAddressOf(), &error_blob ) );

  ERR_FAIL_RET_F( device->CreateRootSignature(
      0,
      root_signature_blob->GetBufferPointer(),
      root_signature_blob->GetBufferSize(),
      IID_PPV_ARGS( out->RootSignature.ReleaseAndGetAddressOf() ) ) );

  CD3DX12_DEPTH_STENCIL_DESC depth_stencil_desc{ D3D12_DEFAULT };
  depth_stencil_desc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

  CD3DX12_RASTERIZER_DESC2 rasterizer_desc{ D3D12_DEFAULT };
  rasterizer_desc.FrontCounterClockwise = TRUE;
  rasterizer_desc.CullMode              = D3D12_CULL_MODE_BACK;

  D3D12_RT_FORMAT_ARRAY rtv_formats{
    .RTFormats        = { DXGI_FORMAT_R8G8B8A8_UNORM },
    .NumRenderTargets = 1,
  };

  struct BackgroundPipelineStream
  {
    CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE        RootSignature;
    CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY    PrimitiveTopologyType;
    CD3DX12_PIPELINE_STATE_STREAM_MS                    MS;
    CD3DX12_PIPELINE_STATE_STREAM_PS                    PS;
    CD3DX12_PIPELINE_STATE_STREAM_RASTERIZER2           Rasterizer;
    CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL         DepthStencil;
    CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS RTVFormats;
    CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL_FORMAT  DSVFormat;
  };

  BackgroundPipelineStream bg_pipeline_stream = {
    .RootSignature         = out->RootSignature.Get(),
    .PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
    .MS                    = CD3DX12_SHADER_BYTECODE( bg_mesh_shader_blob.Get() ),
    .PS                    = CD3DX12_SHADER_BYTECODE( bg_pixel_shader_blob.Get() ),
    .Rasterizer            = rasterizer_desc,
    .DepthStencil          = depth_stencil_desc,
    .RTVFormats            = rtv_formats,
    .DSVFormat             = DXGI_FORMAT_D32_FLOAT,
  };

  D3D12_PIPELINE_STATE_STREAM_DESC const bg_pipeline_state_stream_desc = {
    .SizeInBytes                   = sizeof bg_pipeline_stream,
    .pPipelineStateSubobjectStream = &bg_pipeline_stream,
  };
  ERR_FAIL_RET_F( device->CreatePipelineState(
      &bg_pipeline_state_stream_desc, IID_PPV_ARGS( out->Pipeline.ReleaseAndGetAddressOf() ) ) );

  return true;
}
