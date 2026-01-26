#include "Environment.hpp"

#include <Graphics/RenderDevice.hpp>
#include <Util/DataUtil.hpp>
#include <Util/HelperUtils.hpp>
#include "TextureLoader.hpp"


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
  //
  auto skybox = render_device->CreateTextureCube( {
      .Format    = DXGI_FORMAT_R11G11B10_FLOAT,
      .Side      = kEnvCubeSide,
      .Usage     = TextureUsage::kReadWrite,
      .MipLevels = MipLevels::kAuto,
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
    struct EnvParams
    {
      struct BoundDataType
      {
        SRVHandle InputTextureHandle;
        UAVHandle OutputTextureHandle;
        uint32_t  CubeSide;
      };

      Texture       InputTexture;
      Texture       OutputTexture;
      uint32_t      CubeSide;

      BoundDataType Bind( ResourceBinder* binder ) const
      {
        return {
          .InputTextureHandle  = binder->BindSRV( InputTexture ),
          .OutputTextureHandle = binder->BindUAV( OutputTexture ),
          .CubeSide            = CubeSide,
        };
      }
    };
    static_assert( BindableStructure<EnvParams> );

    struct PrefilterParams
    {
      struct BoundDataType
      {
        SRVHandle Skybox;
        uint32_t  SkyboxSide;
        UAVHandle OutputTexture;
        uint32_t  OutputSide;
        float     Roughness;
      };

      Texture       Skybox;
      uint32_t      SkyboxSide;
      Texture       OutputTexture;
      uint32_t      OutputSide;
      float         Roughness;
      uint32_t      LoD;
      DXGI_FORMAT   Format;

      BoundDataType Bind( ResourceBinder* binder ) const
      {
        auto const desc = CD3DX12_UNORDERED_ACCESS_VIEW_DESC::Tex2DArray( Format, UINT32_MAX, 0, LoD );
        return {
          .Skybox        = binder->BindSRV( Skybox ),
          .SkyboxSide    = SkyboxSide,
          .OutputTexture = binder->BindTransient( OutputTexture, desc ),
          .OutputSide    = OutputSide,
          .Roughness     = Roughness,
        };
      }
    };

    struct BrdfLUTParams
    {
      struct BoundDataType
      {
        UAVHandle OutputTextureHandle;
        uint32_t  Width;
        uint32_t  Height;
      };

      Texture       OutputTexture;
      uint32_t      Width;
      uint32_t      Height;

      BoundDataType Bind( ResourceBinder* binder ) const
      {
        return {
          .OutputTextureHandle = binder->BindUAV( OutputTexture ),
          .Width               = Width,
          .Height              = Height,
        };
      }
    };

    uint32_t constexpr kThreadGroupX    = 16;
    uint32_t constexpr kThreadGroupY    = 16;
    uint32_t constexpr kThreadGroupZ    = 1;

    D3D12_ROOT_PARAMETER1 root_params[] = {
      RootConstants{ .Register = 0, .SizeBytes = 5 * sizeof( uint32_t ) },
    };

    D3D12_STATIC_SAMPLER_DESC static_samplers[] = {
      CD3DX12_STATIC_SAMPLER_DESC{ 0 },
    };

    auto root_signature = render_device->CreateRootSignature( {
        .RootParameters = root_params,
        .StaticSamplers = static_samplers,
        .ShaderAccess   = RootSignatureDesc::Access::kCompute,
        .DebugName      = "Environment Root Signature",
    } );
    if ( not root_signature ) return false;

    auto eqrect_to_cube_pipeline = render_device->CreateComputePipeline( {
        .RootSignature     = root_signature.Get(),
        .ComputeShaderName = "EqrectToCube.cso",
        .DebugName         = "Eqrect -> Cube Pipeline",
    } );
    if ( not eqrect_to_cube_pipeline ) return false;

    auto diffuse_irradiance_pipeline = render_device->CreateComputePipeline( {
        .RootSignature     = root_signature.Get(),
        .ComputeShaderName = "DiffuseIrradiance.cso",
        .DebugName         = "Diffuse Irradiance Pipeline",
    } );
    if ( not diffuse_irradiance_pipeline ) return false;

    auto prefilter_pipeline = render_device->CreateComputePipeline( {
        .RootSignature     = root_signature.Get(),
        .ComputeShaderName = "Prefilter.cso",
        .DebugName         = "Prefilter Pipeline",
    } );
    if ( not prefilter_pipeline ) return false;

    auto brdf_lut_pipeline = render_device->CreateComputePipeline( {
        .RootSignature     = root_signature.Get(),
        .ComputeShaderName = "BrdfLUT.cso",
        .DebugName         = "BRDF LUT Pipeline",
    } );
    if ( not brdf_lut_pipeline ) return false;

    D3D12_RESOURCE_DESC prefilter_desc = prefilter.GetTexture()->GetDesc();
    ASSERT( prefilter_desc.MipLevels == kPrefilterMaxLoD + 1 /* Accounting for mip0 */ );

    Context   context    = render_device->CreateContext( D3D12_COMMAND_LIST_TYPE_COMPUTE );
    auto      desc_heaps = render_device->GetBindlessDescriptorHeaps();

    EnvParams env_cube_root_constant{
      .InputTexture  = environment,
      .OutputTexture = skybox,
      .CubeSide      = kEnvCubeSide,
    };

    EnvParams diffuse_irradiance_root_constant{
      .InputTexture  = skybox,
      .OutputTexture = diffuse_irradiance,
      .CubeSide      = kDiffuseCubeSide,
    };

    PrefilterParams prefilter_constant{
      .Skybox        = skybox,
      .SkyboxSide    = kEnvCubeSide,
      .OutputTexture = prefilter,
      .OutputSide    = kPrefilterCubeSide,
      .Roughness     = 0.0f,
    };

    BrdfLUTParams brdf_lut_constant{
      .OutputTexture = brdf_lut,
      .Width         = kBrdfLutSize,
      .Height        = kBrdfLutSize,
    };

    auto command_list = context.GetCommandList();

    command_list.SetDescriptorHeaps( desc_heaps );
    command_list.SetComputeRootSignature( root_signature.Get() );

    command_list.SetPipelineState( eqrect_to_cube_pipeline.Get() );
    command_list.BindComputeResources( 0, env_cube_root_constant );
    command_list.Dispatch( {
        .X = kEnvCubeSide / kThreadGroupX,
        .Y = kEnvCubeSide / kThreadGroupY,
        .Z = 6 / kThreadGroupZ,
    } );

    command_list.ResourceBarrier( CD3DX12_RESOURCE_BARRIER::UAV( skybox.GetTexture() ) );

    if ( not texture_loader->GetMipMapper()->TryGenerateMipMapCube( &command_list, &skybox ) ) return false;

    command_list.ResourceBarrier( CD3DX12_RESOURCE_BARRIER::UAV( skybox.GetTexture() ) );

    command_list.SetComputeRootSignature( root_signature.Get() );
    command_list.SetPipelineState( diffuse_irradiance_pipeline.Get() );
    command_list.BindComputeResources( 0, diffuse_irradiance_root_constant );
    command_list.Dispatch( {
        .X = kDiffuseCubeSide / kThreadGroupX,
        .Y = kDiffuseCubeSide / kThreadGroupY,
        .Z = 6 / kThreadGroupZ,
    } );

    for ( uint32_t i = 0; i <= kPrefilterMaxLoD; i++ )
    {
      prefilter_constant.LoD       = i;
      prefilter_constant.Roughness = ( float )i / ( float )kPrefilterMaxLoD;

      command_list.SetPipelineState( prefilter_pipeline.Get() );
      command_list.BindComputeResources( 0, prefilter_constant );
      command_list.Dispatch( {
          .X = std::max<uint32_t>( prefilter_constant.OutputSide / kThreadGroupX, 1 ),
          .Y = std::max<uint32_t>( prefilter_constant.OutputSide / kThreadGroupY, 1 ),
          .Z = 6 / kThreadGroupZ,
      } );

      prefilter_constant.OutputSide = std::max<uint32_t>( prefilter_constant.OutputSide / 2, 1 );
    }

    command_list.SetPipelineState( brdf_lut_pipeline.Get() );
    command_list.BindComputeResources( 0, brdf_lut_constant );
    command_list.Dispatch( {
        .X = kBrdfLutSize / kThreadGroupX,
        .Y = kBrdfLutSize / kThreadGroupY,
    } );

    Context::Receipt receipt = context.Submit( std::move( command_list ) );
    context.WaitOn( receipt );
  }

  new ( env ) Environment{
    std::move( skybox ),
    std::move( diffuse_irradiance ),
    std::move( prefilter ),
    std::move( brdf_lut ),
  };

  return true;
}
