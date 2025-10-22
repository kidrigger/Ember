#include "LightingPass.hpp"

#include "Environment.hpp"
#include "RenderPassCommon.hpp"
#include "Util/DataUtil.hpp"

bool Ember::RenderPass::OmniLightDeferred::Create(
    OmniLightDeferred* out, RenderDevice* render_device, DXGI_FORMAT const rt_format )
{
  out->RenderTargetFormat = rt_format;

  ComPtr<ID3DBlob> omni_volume_amp_shader_blob;
  ERR_FAIL_RET_F( D3DReadFileToBlob( L"OmniLightingAS.cso", &omni_volume_amp_shader_blob ) );
  ComPtr<ID3DBlob> omni_volume_mesh_shader_blob;
  ERR_FAIL_RET_F( D3DReadFileToBlob( L"OmniLightingMS.cso", &omni_volume_mesh_shader_blob ) );
  ComPtr<ID3DBlob> omni_volume_pixel_shader_blob;
  ERR_FAIL_RET_F( D3DReadFileToBlob( L"OmniLightingPS.cso", &omni_volume_pixel_shader_blob ) );

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
    CD3DX12_STATIC_SAMPLER_DESC{ 3, D3D12_FILTER_MIN_MAG_MIP_POINT },
  };

  D3D12_ROOT_SIGNATURE_FLAGS const root_signature_flags = D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED |
                                                          D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED |
                                                          D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS |
                                                          D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
                                                          D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
                                                          D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS;

  CD3DX12_ROOT_PARAMETER1 merge_root_parameters[3];
  merge_root_parameters[0].InitAsConstants( 7, 0 );
  merge_root_parameters[1].InitAsConstants( sizeof( PerFrameConstants ) / 4, 1 );
  merge_root_parameters[2].InitAsConstants( sizeof( Environment::GpuRepr ) / 4, 2 );

  CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC merge_root_signature_desc;
  merge_root_signature_desc.Init_1_1(
      CountOf( merge_root_parameters ),
      DataOf( merge_root_parameters ),
      CountOf( static_sampler_desc ),
      DataOf( static_sampler_desc ),
      root_signature_flags );

  ComPtr<ID3DBlob> root_signature_blob;
  ComPtr<ID3DBlob> error_blob;
  ERR_FAIL_RET_F( D3DX12SerializeVersionedRootSignature(
      &merge_root_signature_desc, root_signature_version, &root_signature_blob, &error_blob ) );

  ERR_FAIL_RET_F( device->CreateRootSignature(
      0,
      root_signature_blob->GetBufferPointer(),
      root_signature_blob->GetBufferSize(),
      IID_PPV_ARGS( out->RootSignature.ReleaseAndGetAddressOf() ) ) );

  struct VolumePipelineStream
  {
    CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE        RootSignature;
    CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY    PrimitiveTopologyType;
    CD3DX12_PIPELINE_STATE_STREAM_AS                    AS;
    CD3DX12_PIPELINE_STATE_STREAM_MS                    MS;
    CD3DX12_PIPELINE_STATE_STREAM_PS                    PS;
    CD3DX12_PIPELINE_STATE_STREAM_RASTERIZER2           Rasterizer;
    CD3DX12_PIPELINE_STATE_STREAM_BLEND_DESC            Blending;
    CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS RTVFormats;
    CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL         DepthStencil;
    CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL_FORMAT  DSVFormat;
  };

  CD3DX12_RASTERIZER_DESC2 light_vol_raster_desc{ D3D12_DEFAULT };
  light_vol_raster_desc.FrontCounterClockwise = TRUE;
  light_vol_raster_desc.CullMode              = D3D12_CULL_MODE_FRONT;

  CD3DX12_DEPTH_STENCIL_DESC depth_stencil_desc{ D3D12_DEFAULT };
  depth_stencil_desc.DepthEnable    = TRUE;
  depth_stencil_desc.DepthFunc      = D3D12_COMPARISON_FUNC_GREATER_EQUAL;
  depth_stencil_desc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;

  D3D12_RT_FORMAT_ARRAY final_rt_formats{
    .RTFormats        = { rt_format },
    .NumRenderTargets = 1,
  };

  CD3DX12_BLEND_DESC light_vol_blend_desc{ D3D12_DEFAULT };
  light_vol_blend_desc.RenderTarget[0] = {
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

  VolumePipelineStream volume_pipeline_stream = {
    .RootSignature         = out->RootSignature.Get(),
    .PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
    .AS                    = CD3DX12_SHADER_BYTECODE( omni_volume_amp_shader_blob.Get() ),
    .MS                    = CD3DX12_SHADER_BYTECODE( omni_volume_mesh_shader_blob.Get() ),
    .PS                    = CD3DX12_SHADER_BYTECODE( omni_volume_pixel_shader_blob.Get() ),
    .Rasterizer            = light_vol_raster_desc,
    .Blending              = light_vol_blend_desc,
    .RTVFormats            = final_rt_formats,
    .DepthStencil          = depth_stencil_desc,
    .DSVFormat             = DXGI_FORMAT_D32_FLOAT,
  };

  D3D12_PIPELINE_STATE_STREAM_DESC const volume_pipeline_stream_desc = {
    .SizeInBytes                   = sizeof volume_pipeline_stream,
    .pPipelineStateSubobjectStream = &volume_pipeline_stream,
  };

  ERR_FAIL_RET_F( device->CreatePipelineState(
      &volume_pipeline_stream_desc, IID_PPV_ARGS( out->Pipeline.ReleaseAndGetAddressOf() ) ) );

  return true;
}

bool Ember::RenderPass::SpotLightDeferred::Create(
    SpotLightDeferred* out, RenderDevice* render_device, DXGI_FORMAT rt_format )
{
  out->RenderTargetFormat = rt_format;

  ComPtr<ID3DBlob> spot_volume_amp_shader_blob;
  ERR_FAIL_RET_F( D3DReadFileToBlob( L"SpotLightingAS.cso", &spot_volume_amp_shader_blob ) );
  ComPtr<ID3DBlob> spot_volume_mesh_shader_blob;
  ERR_FAIL_RET_F( D3DReadFileToBlob( L"SpotLightingMS.cso", &spot_volume_mesh_shader_blob ) );
  ComPtr<ID3DBlob> spot_volume_pixel_shader_blob;
  ERR_FAIL_RET_F( D3DReadFileToBlob( L"SpotLightingPS.cso", &spot_volume_pixel_shader_blob ) );

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
    CD3DX12_STATIC_SAMPLER_DESC{ 3, D3D12_FILTER_MIN_MAG_MIP_POINT },
  };

  D3D12_ROOT_SIGNATURE_FLAGS const root_signature_flags = D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED |
                                                          D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED |
                                                          D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS |
                                                          D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
                                                          D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
                                                          D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS;

  CD3DX12_ROOT_PARAMETER1 merge_root_parameters[3];
  merge_root_parameters[0].InitAsConstants( 7, 0 );
  merge_root_parameters[1].InitAsConstants( sizeof( PerFrameConstants ) / 4, 1 );
  merge_root_parameters[2].InitAsConstants( sizeof( Environment::GpuRepr ) / 4, 2 );

  CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC merge_root_signature_desc;
  merge_root_signature_desc.Init_1_1(
      CountOf( merge_root_parameters ),
      DataOf( merge_root_parameters ),
      CountOf( static_sampler_desc ),
      DataOf( static_sampler_desc ),
      root_signature_flags );

  ComPtr<ID3DBlob> root_signature_blob;
  ComPtr<ID3DBlob> error_blob;
  ERR_FAIL_RET_F( D3DX12SerializeVersionedRootSignature(
      &merge_root_signature_desc, root_signature_version, &root_signature_blob, &error_blob ) );

  ERR_FAIL_RET_F( device->CreateRootSignature(
      0,
      root_signature_blob->GetBufferPointer(),
      root_signature_blob->GetBufferSize(),
      IID_PPV_ARGS( out->RootSignature.ReleaseAndGetAddressOf() ) ) );

  struct VolumePipelineStream
  {
    CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE        RootSignature;
    CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY    PrimitiveTopologyType;
    CD3DX12_PIPELINE_STATE_STREAM_AS                    AS;
    CD3DX12_PIPELINE_STATE_STREAM_MS                    MS;
    CD3DX12_PIPELINE_STATE_STREAM_PS                    PS;
    CD3DX12_PIPELINE_STATE_STREAM_RASTERIZER2           Rasterizer;
    CD3DX12_PIPELINE_STATE_STREAM_BLEND_DESC            Blending;
    CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS RTVFormats;
    CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL         DepthStencil;
    CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL_FORMAT  DSVFormat;
  };

  CD3DX12_RASTERIZER_DESC2 light_vol_raster_desc{ D3D12_DEFAULT };
  light_vol_raster_desc.FrontCounterClockwise = TRUE;
  light_vol_raster_desc.CullMode              = D3D12_CULL_MODE_FRONT;

  CD3DX12_DEPTH_STENCIL_DESC depth_stencil_desc{ D3D12_DEFAULT };
  depth_stencil_desc.DepthEnable    = TRUE;
  depth_stencil_desc.DepthFunc      = D3D12_COMPARISON_FUNC_GREATER_EQUAL;
  depth_stencil_desc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;

  D3D12_RT_FORMAT_ARRAY final_rt_formats{
    .RTFormats        = { rt_format },
    .NumRenderTargets = 1,
  };

  CD3DX12_BLEND_DESC light_vol_blend_desc{ D3D12_DEFAULT };
  light_vol_blend_desc.RenderTarget[0] = {
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

  VolumePipelineStream volume_pipeline_stream = {
    .RootSignature         = out->RootSignature.Get(),
    .PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
    .AS                    = CD3DX12_SHADER_BYTECODE( spot_volume_amp_shader_blob.Get() ),
    .MS                    = CD3DX12_SHADER_BYTECODE( spot_volume_mesh_shader_blob.Get() ),
    .PS                    = CD3DX12_SHADER_BYTECODE( spot_volume_pixel_shader_blob.Get() ),
    .Rasterizer            = light_vol_raster_desc,
    .Blending              = light_vol_blend_desc,
    .RTVFormats            = final_rt_formats,
    .DepthStencil          = depth_stencil_desc,
    .DSVFormat             = DXGI_FORMAT_D32_FLOAT,
  };

  D3D12_PIPELINE_STATE_STREAM_DESC const volume_pipeline_stream_desc = {
    .SizeInBytes                   = sizeof volume_pipeline_stream,
    .pPipelineStateSubobjectStream = &volume_pipeline_stream,
  };

  ERR_FAIL_RET_F( device->CreatePipelineState(
      &volume_pipeline_stream_desc, IID_PPV_ARGS( out->Pipeline.ReleaseAndGetAddressOf() ) ) );

  return true;
}

bool Ember::RenderPass::ScreenSpaceLightDeferred::Create(
    ScreenSpaceLightDeferred* out, RenderDevice* render_device, DXGI_FORMAT rt_format )
{
  out->RenderTargetFormat = rt_format;

  ComPtr<ID3DBlob> merge_mesh_shader_blob;
  ERR_FAIL_RET_F( D3DReadFileToBlob( L"LightingMS.cso", &merge_mesh_shader_blob ) );
  ComPtr<ID3DBlob> merge_pixel_shader_blob;
  ERR_FAIL_RET_F( D3DReadFileToBlob( L"LightingPS.cso", &merge_pixel_shader_blob ) );

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
    CD3DX12_STATIC_SAMPLER_DESC{ 3, D3D12_FILTER_MIN_MAG_MIP_POINT },
  };

  D3D12_ROOT_SIGNATURE_FLAGS const root_signature_flags = D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED |
                                                          D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED |
                                                          D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS |
                                                          D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
                                                          D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
                                                          D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS;

  CD3DX12_ROOT_PARAMETER1 merge_root_parameters[3];
  merge_root_parameters[0].InitAsConstants( 7, 0 );
  merge_root_parameters[1].InitAsConstants( sizeof( PerFrameConstants ) / 4, 1 );
  merge_root_parameters[2].InitAsConstants( sizeof( Environment::GpuRepr ) / 4, 2 );

  CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC merge_root_signature_desc;
  merge_root_signature_desc.Init_1_1(
      CountOf( merge_root_parameters ),
      DataOf( merge_root_parameters ),
      CountOf( static_sampler_desc ),
      DataOf( static_sampler_desc ),
      root_signature_flags );

  ComPtr<ID3DBlob> root_signature_blob;
  ComPtr<ID3DBlob> error_blob;
  ERR_FAIL_RET_F( D3DX12SerializeVersionedRootSignature(
      &merge_root_signature_desc, root_signature_version, &root_signature_blob, &error_blob ) );

  ERR_FAIL_RET_F( device->CreateRootSignature(
      0,
      root_signature_blob->GetBufferPointer(),
      root_signature_blob->GetBufferSize(),
      IID_PPV_ARGS( out->RootSignature.ReleaseAndGetAddressOf() ) ) );

  struct MergePipelineStream
  {
    CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE        RootSignature;
    CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY    PrimitiveTopologyType;
    CD3DX12_PIPELINE_STATE_STREAM_MS                    MS;
    CD3DX12_PIPELINE_STATE_STREAM_PS                    PS;
    CD3DX12_PIPELINE_STATE_STREAM_BLEND_DESC            Blending;
    CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS RTVFormats;
  };

  D3D12_RT_FORMAT_ARRAY final_rt_formats{
    .RTFormats        = { rt_format },
    .NumRenderTargets = 1,
  };
  CD3DX12_BLEND_DESC light_vol_blend_desc{ D3D12_DEFAULT };
  light_vol_blend_desc.RenderTarget[0] = {
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

  MergePipelineStream merge_pipeline_stream = {
    .RootSignature         = out->RootSignature.Get(),
    .PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
    .MS                    = CD3DX12_SHADER_BYTECODE( merge_mesh_shader_blob.Get() ),
    .PS                    = CD3DX12_SHADER_BYTECODE( merge_pixel_shader_blob.Get() ),
    .Blending              = light_vol_blend_desc,
    .RTVFormats            = final_rt_formats,
  };

  D3D12_PIPELINE_STATE_STREAM_DESC const merge_pipeline_state_stream_desc = {
    .SizeInBytes                   = sizeof merge_pipeline_stream,
    .pPipelineStateSubobjectStream = &merge_pipeline_stream,
  };

  ERR_FAIL_RET_F( device->CreatePipelineState(
      &merge_pipeline_state_stream_desc, IID_PPV_ARGS( out->Pipeline.ReleaseAndGetAddressOf() ) ) );

  return true;
}
