#include "Environment.hpp"

#include <Graphics/RenderDevice.hpp>
#include <Util/DataUtil.hpp>
#include <Util/HelperUtils.hpp>
#include "TextureLoader.hpp"

namespace Ember
{
namespace
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
static_assert( BindableStructure<PrefilterParams> );

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
static_assert( BindableStructure<BrdfLUTParams> );

struct EnvContext
{
  RenderDevice*                 RenderDevice;
  CommandList*                  CommandList;
  Environment::Pipelines const* Pipelines;
};

bool GenerateSkybox(
    Texture* skybox, EnvContext const& context, MipMapGenerator const* mipmapper, Texture const& environment )
{
  uint32_t constexpr kThreadGroupX = 16;
  uint32_t constexpr kThreadGroupY = 16;
  uint32_t constexpr kThreadGroupZ = 1;

  // Generate skybox.
  *skybox = context.RenderDevice->CreateTextureCube( {
      .Format    = DXGI_FORMAT_R11G11B10_FLOAT,
      .Side      = Environment::kEnvCubeSide,
      .Usage     = TextureUsage::kReadWrite,
      .MipLevels = MipLevels::kAuto,
      .InitState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
  } );
  skybox->SetName( L"Skybox" );

  EnvParams const env_cube_root_constant{
    .InputTexture  = environment,
    .OutputTexture = *skybox,
    .CubeSide      = Environment::kEnvCubeSide,
  };

  context.CommandList->SetComputeRootSignature( context.Pipelines->RootSignature.Get() );
  context.CommandList->SetPipelineState( context.Pipelines->EqRectToCubePipeline.Get() );
  context.CommandList->BindComputeResources( 0, env_cube_root_constant );
  context.CommandList->Dispatch( {
      .X = Environment::kEnvCubeSide / kThreadGroupX,
      .Y = Environment::kEnvCubeSide / kThreadGroupY,
      .Z = 6 / kThreadGroupZ,
  } );

  context.CommandList->ResourceBarrier( CD3DX12_RESOURCE_BARRIER::UAV( skybox->GetTexture() ) );

  if ( not mipmapper->TryGenerateMipMapCube( context.CommandList, skybox ) ) return false;

  context.CommandList->ResourceBarrier( CD3DX12_RESOURCE_BARRIER::UAV( skybox->GetTexture() ) );

  return true;
};

bool CreatePipelines( Environment::Pipelines* out, RenderDevice* render_device )
{
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

  new ( out ) Environment::Pipelines{
    std::move( root_signature ),
    std::move( eqrect_to_cube_pipeline ),
    std::move( diffuse_irradiance_pipeline ),
    std::move( prefilter_pipeline ),
  };

  return true;
};

bool GenerateDiffuseIrradiance( Texture* diffuse_irradiance, EnvContext const& context, Texture const& skybox )
{
  uint32_t constexpr kThreadGroupX = 16;
  uint32_t constexpr kThreadGroupY = 16;
  uint32_t constexpr kThreadGroupZ = 1;

  // Create texture
  *diffuse_irradiance = context.RenderDevice->CreateTextureCube( {
      .Format    = DXGI_FORMAT_R11G11B10_FLOAT,
      .Side      = Environment::kDiffuseCubeSide,
      .Usage     = TextureUsage::kReadWrite,
      .MipLevels = MipLevels::kBase,
      .InitState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
  } );
  diffuse_irradiance->SetName( L"Diffuse Irradiance Map" );

  EnvParams const diffuse_irradiance_root_constant{
    .InputTexture  = skybox,
    .OutputTexture = *diffuse_irradiance,
    .CubeSide      = Environment::kDiffuseCubeSide,
  };

  context.CommandList->SetComputeRootSignature( context.Pipelines->RootSignature.Get() );
  context.CommandList->SetPipelineState( context.Pipelines->DiffuseIrradiance.Get() );
  context.CommandList->BindComputeResources( 0, diffuse_irradiance_root_constant );
  context.CommandList->Dispatch( {
      .X = Environment::kDiffuseCubeSide / kThreadGroupX,
      .Y = Environment::kDiffuseCubeSide / kThreadGroupY,
      .Z = 6 / kThreadGroupZ,
  } );

  return true;
};

bool GeneratePrefilter( Texture* prefilter, EnvContext const& context, Texture const& skybox )
{
  size_t constexpr static kThreadGroupX = 16;
  size_t constexpr static kThreadGroupY = 16;
  size_t constexpr static kThreadGroupZ = 1;

  // Create texture
  *prefilter = context.RenderDevice->CreateTextureCube( {
      .Format    = DXGI_FORMAT_R11G11B10_FLOAT,
      .Side      = Environment::kPrefilterCubeSide,
      .Usage     = TextureUsage::kReadWrite,
      .MipLevels = Environment::kPrefilterMaxLoD + 1, // accounting for mip0
      .InitState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
  } );
  prefilter->SetName( L"Prefiltered Cube" );

  PrefilterParams prefilter_constant{
    .Skybox        = skybox,
    .SkyboxSide    = Environment::kEnvCubeSide,
    .OutputTexture = *prefilter,
    .OutputSide    = Environment::kPrefilterCubeSide,
    .Roughness     = 0.0f,
  };

  ASSERT( prefilter->GetTexture()->GetDesc().MipLevels == kPrefilterMaxLoD + 1 /* Accounting for mip0 */ );

  context.CommandList->SetComputeRootSignature( context.Pipelines->RootSignature.Get() );
  context.CommandList->SetPipelineState( context.Pipelines->Prefilter.Get() );

  for ( uint32_t i = 0; i <= Environment::kPrefilterMaxLoD; i++ )
  {
    prefilter_constant.LoD       = i;
    prefilter_constant.Roughness = ( float )i / ( float )Environment::kPrefilterMaxLoD;

    context.CommandList->BindComputeResources( 0, prefilter_constant );
    context.CommandList->Dispatch( {
        .X = std::max<uint32_t>( prefilter_constant.OutputSide / kThreadGroupX, 1 ),
        .Y = std::max<uint32_t>( prefilter_constant.OutputSide / kThreadGroupY, 1 ),
        .Z = 6 / kThreadGroupZ,
    } );

    prefilter_constant.OutputSide = std::max<uint32_t>( prefilter_constant.OutputSide / 2, 1 );
  }

  return true;
}

bool GenerateBrdfLUT( Texture* brdf_lut, EnvContext const& context )
{
  size_t constexpr static kThreadGroupX = 16;
  size_t constexpr static kThreadGroupY = 16;

  // Create BRDF LUT texture.
  *brdf_lut = context.RenderDevice->CreateTexture2D( {
      .Format    = DXGI_FORMAT_R16G16_FLOAT,
      .Width     = Environment::kBrdfLUTSize,
      .Height    = Environment::kBrdfLUTSize,
      .Usage     = TextureUsage::kReadWrite,
      .MipLevels = MipLevels::kBase,
      .InitState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
  } );
  brdf_lut->SetName( L"BRDF LUT" );

  auto brdf_lut_pipeline = context.RenderDevice->CreateComputePipeline( {
      .RootSignature     = context.Pipelines->RootSignature.Get(),
      .ComputeShaderName = "BrdfLUT.cso",
      .DebugName         = "BRDF LUT Pipeline",
  } );
  if ( not brdf_lut_pipeline ) return false;

  BrdfLUTParams const brdf_lut_constant{
    .OutputTexture = *brdf_lut,
    .Width         = Environment::kBrdfLUTSize,
    .Height        = Environment::kBrdfLUTSize,
  };

  context.CommandList->SetPipelineState( brdf_lut_pipeline.Get() );
  context.CommandList->BindComputeResources( 0, brdf_lut_constant );
  context.CommandList->Dispatch( {
      .X = Environment::kBrdfLUTSize / kThreadGroupX,
      .Y = Environment::kBrdfLUTSize / kThreadGroupY,
  } );

  return true;
}

bool CreateIBLEnvironment( Environment::IBLEnvironment* ibl, EnvContext const& context, Texture skybox )
{
  Texture diffuse_irradiance;
  if ( not GenerateDiffuseIrradiance( &diffuse_irradiance, context, skybox ) ) return false;

  Texture prefilter;
  if ( not GeneratePrefilter( &prefilter, context, skybox ) ) return false;

  new ( ibl ) Environment::IBLEnvironment{
    std::move( skybox ),
    std::move( diffuse_irradiance ),
    std::move( prefilter ),
  };

  return true;
};
} // namespace
} // namespace Ember

Ember::Environment::Environment( IBLEnvironment ibl, Pipelines pipelines, Texture brdf_lut )
  : m_FallbackIBL{ std::move( ibl ) }
  , m_Pipelines{ std::move( pipelines ) }
  , m_BrdfLUT{ std::move( brdf_lut ) }
  , m_Repr{
    .Skybox            = m_FallbackIBL.Skybox.GetSRVHandle(),
    .DiffuseIrradiance = m_FallbackIBL.DiffuseIrradiance.GetSRVHandle(),
    .Prefilter         = m_FallbackIBL.Prefilter.GetSRVHandle(),
    .BrdfLUT           = m_BrdfLUT.GetSRVHandle(),
  }
{}

Ember::Environment::GpuRepr const& Ember::Environment::Repr() const
{
  return m_Repr;
}

bool Ember::Environment::TryLoadFromFile( Environment* env, LoadFromFile const& args )
{
  RenderDevice*  render_device  = args.RenderDevice;
  TextureLoader* texture_loader = args.TextureLoader;
  char const*    env_map_file   = args.FileName;

  Texture        environment;
  if ( not texture_loader->TryLoadTexture( &environment, env_map_file ) ) return false;
  render_device->WaitOn( texture_loader->EndBatch() );

  return TryLoadFromEqRect( env, { render_device, texture_loader->GetMipMapper(), std::move( environment ) } );
}

bool Ember::Environment::TryLoadFromEqRect( Environment* env, LoadFromEqRect const& args )
{
  RenderDevice*    render_device = args.RenderDevice;
  MipMapGenerator* mip_mapper    = args.MipMapper;
  Texture const&   environment   = args.EqrectTexture;

  Queue            queue         = render_device->CreateQueue( D3D12_COMMAND_LIST_TYPE_COMPUTE );
  auto             command_list  = queue.GetCommandList();

  Pipelines        pipelines;
  if ( not CreatePipelines( &pipelines, render_device ) ) return false;

  EnvContext const context{
    .RenderDevice = render_device,
    .CommandList  = &command_list,
    .Pipelines    = &pipelines,
  };

  Texture        skybox;
  IBLEnvironment ibl;
  Texture        brdf_lut;

  if ( not GenerateSkybox( &skybox, context, mip_mapper, environment ) ) return false;
  if ( not CreateIBLEnvironment( &ibl, context, std::move( skybox ) ) ) return false;
  if ( not GenerateBrdfLUT( &brdf_lut, context ) ) return false;

  Queue::Receipt receipt = queue.Submit( std::move( command_list ) );
  queue.WaitOn( receipt );

  new ( env ) Environment{
    std::move( ibl ),
    std::move( pipelines ),
    std::move( brdf_lut ),
  };

  return true;
}

bool Ember::Environment::TryLoadFromCube( Environment* env, LoadFromCube const& args )
{
  RenderDevice* render_device = args.RenderDevice;

  Queue         queue         = render_device->CreateQueue( D3D12_COMMAND_LIST_TYPE_COMPUTE );
  auto          command_list  = queue.GetCommandList();

  Pipelines     pipelines;
  if ( not CreatePipelines( &pipelines, render_device ) ) return false;

  EnvContext const context{
    .RenderDevice = render_device,
    .CommandList  = &command_list,
    .Pipelines    = &pipelines,
  };

  Texture        skybox = args.CubeTexture;
  IBLEnvironment ibl;
  Texture        brdf_lut;

  if ( not CreateIBLEnvironment( &ibl, context, std::move( skybox ) ) ) return false;
  if ( not GenerateBrdfLUT( &brdf_lut, context ) ) return false;

  Queue::Receipt receipt = queue.Submit( std::move( command_list ) );
  queue.WaitOn( receipt );

  new ( env ) Environment{
    std::move( ibl ),
    std::move( pipelines ),
    std::move( brdf_lut ),
  };

  return true;
}
