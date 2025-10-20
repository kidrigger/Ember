#include "Environment.hpp"

#include "RenderDevice.hpp"
#include "Util/DataUtil.hpp"
#include "Util/HelperUtils.hpp"


Ember::Environment::Environment( Texture skybox, Texture diffuse_irradiance, Texture prefilter, Texture brdf_lut )
  : m_Skybox{ std::move( skybox ) }
  , m_DiffuseIrradiance{ std::move( diffuse_irradiance ) }
  , m_Prefilter{ std::move( prefilter ) }
  , m_BrdfLUT{ std::move( brdf_lut ) }
  , m_Repr{
    .Skybox            = m_Skybox.GetSRVHandle(),
    .DiffuseIrradiance = m_DiffuseIrradiance.GetSRVHandle(),
    .Prefilter         = m_Prefilter.GetSRVHandle(),
    .BrdfLUT           = m_BrdfLUT.GetSRVHandle(),
  }
{}

Ember::Environment::GpuRepr const& Ember::Environment::Repr() const
{
  return m_Repr;
}

bool Ember::Environment::TryLoadFrom(
    Environment* env, RenderDevice* render_device, TextureLoader* texture_loader, char const* const env_map_file )
{
  // Setup Environment
  uint32_t constexpr kEnvCubeSide       = 512;
  uint32_t constexpr kDiffuseCubeSide   = 256;
  uint32_t constexpr kPrefilterCubeSide = 512;
  uint32_t constexpr kPrefilterMaxLoD   = 5;
  uint32_t constexpr kBrdfLutSize       = 512;

  Texture environment;
  if ( not texture_loader->TryLoadTexture( &environment, env_map_file ) ) return false;
  render_device->WaitOn( texture_loader->EndBatch() );

  byte                                buffer[2048];
  std::pmr::monotonic_buffer_resource mbr{ DataOf( buffer ), ByteSizeOf( buffer ), std::pmr::null_memory_resource() };
  ResourceTracker                     tracker{ render_device, &mbr };
  //
  auto skybox = render_device->CreateTextureCube( {
      .Format    = DXGI_FORMAT_R11G11B10_FLOAT,
      .Side      = kEnvCubeSide,
      .Usage     = TextureUsage::kReadWrite,
      .MipLevels = MipLevels::kBase,
      .InitState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
  } );
  skybox.SetName( L"Skybox" );

  auto diffuse_irradiance = render_device->CreateTextureCube( {
      .Format    = DXGI_FORMAT_R11G11B10_FLOAT,
      .Side      = kDiffuseCubeSide,
      .Usage     = TextureUsage::kReadWrite,
      .MipLevels = MipLevels::kBase,
      .InitState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
  } );
  diffuse_irradiance.SetName( L"Diffuse Irradiance Map" );

  auto prefilter = render_device->CreateTextureCube( {
      .Format    = DXGI_FORMAT_R11G11B10_FLOAT,
      .Side      = kPrefilterCubeSide,
      .Usage     = TextureUsage::kReadWrite,
      .MipLevels = kPrefilterMaxLoD + 1, // accounting for mip0
      .InitState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
  } );
  prefilter.SetName( L"Prefiltered Cube" );

  auto brdf_lut = render_device->CreateTexture2D( {
      .Format    = DXGI_FORMAT_R16G16_FLOAT,
      .Width     = kBrdfLutSize,
      .Height    = kBrdfLutSize,
      .Usage     = TextureUsage::kReadWrite,
      .MipLevels = MipLevels::kBase,
      .InitState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
  } );
  brdf_lut.SetName( L"BRDF LUT" );

  {

    struct EnvRootConstant
    {
      SRVHandle InputTextureHandle;
      UAVHandle OutputTextureHandle;
      uint32_t  CubeSide;
    };

    struct PrefilterConstant
    {
      SRVHandle Skybox;
      uint32_t  SkyboxSide;
      UAVHandle OutputTextureHandle;
      uint32_t  OutputSide;
      float     Roughness;
    };

    struct BrdfLUTConstant
    {
      UAVHandle OutputTextureHandle;
      uint32_t  Width;
      uint32_t  Height;
    };

    CD3DX12_ROOT_PARAMETER1 root_parameters[1];
    root_parameters[0].InitAsConstants( 5, 0 );

    CD3DX12_STATIC_SAMPLER_DESC static_sampler_desc[] = {
      CD3DX12_STATIC_SAMPLER_DESC{ 0 },
    };

    D3D12_ROOT_SIGNATURE_FLAGS const root_signature_flags =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
        D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED |
        D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_MESH_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_AMPLIFICATION_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS;

    uint32_t constexpr kThreadGroupX = 16;
    uint32_t constexpr kThreadGroupY = 16;
    uint32_t constexpr kThreadGroupZ = 1;

    ComPtr<ID3DBlob> eqrect_to_cube_shader;
    ERR_FAIL_RET_F( D3DReadFileToBlob( L"EqrectToCube.cso", &eqrect_to_cube_shader ) );

    ComPtr<ID3DBlob> diffuse_irradiance_shader;
    ERR_FAIL_RET_F( D3DReadFileToBlob( L"DiffuseIrradiance.cso", &diffuse_irradiance_shader ) );

    ComPtr<ID3DBlob> prefilter_shader;
    ERR_FAIL_RET_F( D3DReadFileToBlob( L"Prefilter.cso", &prefilter_shader ) );

    ComPtr<ID3DBlob> brdf_lut_shader;
    ERR_FAIL_RET_F( D3DReadFileToBlob( L"BrdfLUT.cso", &brdf_lut_shader ) );

    ComPtr<ID3D12Device2> device = render_device->GetDevice();

    Context               context;
    Context::Create( &context, device, D3D12_COMMAND_LIST_TYPE_COMPUTE );

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC versioned_root_signature_desc;
    versioned_root_signature_desc.Init_1_1(
        CountOf( root_parameters ),
        DataOf( root_parameters ),
        CountOf( static_sampler_desc ),
        DataOf( static_sampler_desc ),
        root_signature_flags );

    D3D_ROOT_SIGNATURE_VERSION root_signature_version = render_device->FetchHighestRootSignatureVersion();

    ComPtr<ID3DBlob>           root_signature_blob;
    ComPtr<ID3DBlob>           error_blob;
    ERR_FAIL_RET_F( D3DX12SerializeVersionedRootSignature(
        &versioned_root_signature_desc, root_signature_version, &root_signature_blob, &error_blob ) );

    ComPtr<ID3D12RootSignature> root_signature;
    ERR_FAIL_RET_F( device->CreateRootSignature(
        0,
        root_signature_blob->GetBufferPointer(),
        root_signature_blob->GetBufferSize(),
        IID_PPV_ARGS( &root_signature ) ) );

    struct EnvPipelineStream
    {
      CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE RootSignature;
      CD3DX12_PIPELINE_STATE_STREAM_CS             ComputeShader;
    };

    EnvPipelineStream pipeline_stream{
      .RootSignature = root_signature.Get(),
    };

    D3D12_PIPELINE_STATE_STREAM_DESC desc{
      .SizeInBytes                   = sizeof pipeline_stream,
      .pPipelineStateSubobjectStream = &pipeline_stream,
    };

    pipeline_stream.ComputeShader = CD3DX12_SHADER_BYTECODE( eqrect_to_cube_shader.Get() );
    ComPtr<ID3D12PipelineState> eqrect_to_cube_pipeline;
    ERR_FAIL_RET_F( device->CreatePipelineState( &desc, IID_PPV_ARGS( &eqrect_to_cube_pipeline ) ) );
    ERR_FAIL_RET_F( eqrect_to_cube_pipeline->SetName( L"Eqrect -> Cube Pipeline" ) );

    pipeline_stream.ComputeShader = CD3DX12_SHADER_BYTECODE( diffuse_irradiance_shader.Get() );
    ComPtr<ID3D12PipelineState> diffuse_irradiance_pipeline;
    ERR_FAIL_RET_F( device->CreatePipelineState( &desc, IID_PPV_ARGS( &diffuse_irradiance_pipeline ) ) );
    ERR_FAIL_RET_F( diffuse_irradiance_pipeline->SetName( L"Diffuse Irradiance Pipeline" ) );

    pipeline_stream.ComputeShader = CD3DX12_SHADER_BYTECODE( prefilter_shader.Get() );
    ComPtr<ID3D12PipelineState> prefilter_pipeline;
    ERR_FAIL_RET_F( device->CreatePipelineState( &desc, IID_PPV_ARGS( &prefilter_pipeline ) ) );
    ERR_FAIL_RET_F( prefilter_pipeline->SetName( L"Prefilter Pipeline" ) );

    pipeline_stream.ComputeShader = CD3DX12_SHADER_BYTECODE( brdf_lut_shader.Get() );
    ComPtr<ID3D12PipelineState> brdf_lut_pipeline;
    ERR_FAIL_RET_F( device->CreatePipelineState( &desc, IID_PPV_ARGS( &brdf_lut_pipeline ) ) );
    ERR_FAIL_RET_F( brdf_lut_pipeline->SetName( L"BRDF LUT Pipeline" ) );

    D3D12_RESOURCE_DESC prefilter_desc = prefilter.GetTexture()->GetDesc();
    ASSERT( prefilter_desc.MipLevels == kPrefilterMaxLoD + 1 /* Accounting for mip0 */ );

    std::vector<UAVHandle> prefilter_write_handles;
    for ( uint32_t i = 0; i <= kPrefilterMaxLoD; i++ )
    {
      auto      uav_desc   = CD3DX12_UNORDERED_ACCESS_VIEW_DESC::Tex2DArray( prefilter_desc.Format, ( UINT )-1, 0, i );
      UAVHandle uav_handle = render_device->CreateBindlessHandle( prefilter.GetTexture(), uav_desc );
      tracker.PushHandle( uav_handle );
      prefilter_write_handles.push_back( uav_handle );
    }

    auto            desc_heaps = render_device->GetBindlessDescriptorHeaps();

    EnvRootConstant env_cube_root_constant{
      .InputTextureHandle  = environment.GetSRVHandle(),
      .OutputTextureHandle = skybox.GetUAVHandle(),
      .CubeSide            = kEnvCubeSide,
    };

    EnvRootConstant diffuse_irradiance_root_constant{
      .InputTextureHandle  = skybox.GetSRVHandle(),
      .OutputTextureHandle = diffuse_irradiance.GetUAVHandle(),
      .CubeSide            = kDiffuseCubeSide,
    };

    PrefilterConstant prefilter_constant{
      .Skybox              = skybox.GetSRVHandle(),
      .SkyboxSide          = kEnvCubeSide,
      .OutputTextureHandle = prefilter.GetUAVHandle(),
      .OutputSide          = kPrefilterCubeSide,
      .Roughness           = 0.0f,
    };

    BrdfLUTConstant brdf_lut_constant{
      .OutputTextureHandle = brdf_lut.GetUAVHandle(),
      .Width               = kBrdfLutSize,
      .Height              = kBrdfLutSize,
    };

    auto command_list = context.GetCommandList();

    command_list->SetDescriptorHeaps( CountOf( desc_heaps ), DataOf( desc_heaps ) );
    command_list->SetComputeRootSignature( root_signature.Get() );

    command_list->SetPipelineState( eqrect_to_cube_pipeline.Get() );
    command_list->SetComputeRoot32BitConstants( 0, sizeof( EnvRootConstant ) / 4, &env_cube_root_constant, 0 );
    command_list->Dispatch( kEnvCubeSide / kThreadGroupX, kEnvCubeSide / kThreadGroupY, 6 / kThreadGroupZ );

    {
      auto barrier = CD3DX12_RESOURCE_BARRIER::UAV( skybox.GetTexture() );
      command_list->ResourceBarrier( 1, &barrier );
    }

    if ( not texture_loader->TryGenerateMipMapCube(
             command_list.Get(), &skybox, &tracker, D3D12_RESOURCE_STATE_UNORDERED_ACCESS ) )
      return false;

    {
      auto barrier = CD3DX12_RESOURCE_BARRIER::UAV( skybox.GetTexture() );
      command_list->ResourceBarrier( 1, &barrier );
    }

    command_list->SetComputeRootSignature( root_signature.Get() );
    command_list->SetPipelineState( diffuse_irradiance_pipeline.Get() );
    command_list->SetComputeRoot32BitConstants(
        0, sizeof( EnvRootConstant ) / 4, &diffuse_irradiance_root_constant, 0 );
    command_list->Dispatch( kDiffuseCubeSide / kThreadGroupX, kDiffuseCubeSide / kThreadGroupY, 6 / kThreadGroupZ );

    for ( uint32_t i = 0; i <= kPrefilterMaxLoD; i++ )
    {
      prefilter_constant.OutputTextureHandle = prefilter_write_handles[i];
      prefilter_constant.Roughness           = ( float )i / ( float )kPrefilterMaxLoD;

      command_list->SetPipelineState( prefilter_pipeline.Get() );
      command_list->SetComputeRoot32BitConstants( 0, sizeof( PrefilterConstant ) / 4, &prefilter_constant, 0 );
      command_list->Dispatch(
          std::max<uint32_t>( prefilter_constant.OutputSide / kThreadGroupX, 1 ),
          std::max<uint32_t>( prefilter_constant.OutputSide / kThreadGroupY, 1 ),
          6 / kThreadGroupZ );

      prefilter_constant.OutputSide = std::max<uint32_t>( prefilter_constant.OutputSide / 2, 1 );
    }

    command_list->SetPipelineState( brdf_lut_pipeline.Get() );
    command_list->SetComputeRoot32BitConstants( 0, sizeof( BrdfLUTConstant ) / 4, &brdf_lut_constant, 0 );
    command_list->Dispatch( kBrdfLutSize / kThreadGroupX, kBrdfLutSize / kThreadGroupY, 1 );

    Context::Receipt receipt = context.Submit( std::move( command_list ) );
    context.WaitOn( receipt );

    tracker.Clear( nullptr );
  }

  new ( env ) Environment{
    std::move( skybox ),
    std::move( diffuse_irradiance ),
    std::move( prefilter ),
    std::move( brdf_lut ),
  };

  return true;
}
