#include "Environment.hpp"

#include <Graphics/RenderDevice.hpp>
#include <Util/DataUtil.hpp>
#include <Util/HelperUtils.hpp>
#include <Util/Profiling.hpp>
#include <Util/StringUtil.hpp>
#include <fg/Blackboard.hpp>
#include <format>
#include "Render/DrawList.hpp"
#include "RenderPassCommon.hpp"
#include "Scene.hpp"
#include "TextureLoader.hpp"

namespace Ember
{
namespace
{
struct ProbeInfo
{
  DirectX::XMFLOAT3 Position;
  float             CaptureRadius;
};

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

Texture GenerateSkybox( EnvContext const& context, MipMapGenerator const* mipmapper, Texture const& environment )
{
  uint32_t constexpr kThreadGroupX = 16;
  uint32_t constexpr kThreadGroupY = 16;
  uint32_t constexpr kThreadGroupZ = 1;

  // Generate skybox.
  auto skybox = context.RenderDevice->CreateTextureCube( {
      .Format      = DXGI_FORMAT_R11G11B10_FLOAT,
      .Side        = Environment::kEnvCubeSide,
      .Type        = TextureType::kSampled,
      .IsReadWrite = true,
      .MipLevels   = MipLevels::kAuto,
      .InitState   = D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
  } );
  skybox.SetName( L"Skybox" );

  EnvParams const env_cube_root_constant{
    .InputTexture  = environment,
    .OutputTexture = skybox,
    .CubeSide      = Environment::kEnvCubeSide,
  };

  context.CommandList->SetComputeRootSignature( context.Pipelines->IBLRootSignature.Get() );
  context.CommandList->SetPipelineState( context.Pipelines->EqRectToCubePipeline.Get() );
  context.CommandList->BindComputeResources( 0, env_cube_root_constant );
  context.CommandList->Dispatch( {
      .X = Environment::kEnvCubeSide / kThreadGroupX,
      .Y = Environment::kEnvCubeSide / kThreadGroupY,
      .Z = 6 / kThreadGroupZ,
  } );

  context.CommandList->ResourceBarrier( CD3DX12_RESOURCE_BARRIER::UAV( skybox.GetTexture() ) );

  ENSURE( mipmapper->TryGenerateMipMapCube( context.CommandList, &skybox ) );

  context.CommandList->ResourceBarrier( CD3DX12_RESOURCE_BARRIER::UAV( skybox.GetTexture() ) );

  return skybox;
};

bool CreatePipelines( Environment::Pipelines* out, RenderDevice* render_device )
{
  // IBL related constructions
  D3D12_ROOT_PARAMETER1 ibl_root_params[] = {
    RootConstants{ .Register = 0, .SizeBytes = 5 * sizeof( uint32_t ) },
  };

  D3D12_STATIC_SAMPLER_DESC ibl_static_samplers[] = {
    CD3DX12_STATIC_SAMPLER_DESC{ 0 },
  };

  auto ibl_root_signature = render_device->CreateRootSignature( {
      .RootParameters = ibl_root_params,
      .StaticSamplers = ibl_static_samplers,
      .ShaderAccess   = RootSignatureDesc::Access::kCompute,
      .DebugName      = "Environment Root Signature",
  } );
  if ( not ibl_root_signature ) return false;

  auto eqrect_to_cube_pipeline = render_device->CreateComputePipeline( {
      .RootSignature     = ibl_root_signature.Get(),
      .ComputeShaderName = "EqrectToCube.cso",
      .DebugName         = "Eqrect -> Cube Pipeline",
  } );
  if ( not eqrect_to_cube_pipeline ) return false;

  auto diffuse_irradiance_pipeline = render_device->CreateComputePipeline( {
      .RootSignature     = ibl_root_signature.Get(),
      .ComputeShaderName = "DiffuseIrradiance.cso",
      .DebugName         = "Diffuse Irradiance Pipeline",
  } );
  if ( not diffuse_irradiance_pipeline ) return false;

  auto prefilter_pipeline = render_device->CreateComputePipeline( {
      .RootSignature     = ibl_root_signature.Get(),
      .ComputeShaderName = "Prefilter.cso",
      .DebugName         = "Prefilter Pipeline",
  } );
  if ( not prefilter_pipeline ) return false;

  auto brdf_lut_pipeline = render_device->CreateComputePipeline( {
      .RootSignature     = ibl_root_signature.Get(),
      .ComputeShaderName = "BrdfLUT.cso",
      .DebugName         = "BRDF LUT Pipeline",
  } );
  if ( not brdf_lut_pipeline ) return false;

  //
  D3D12_STATIC_SAMPLER_DESC probe_static_sampler_desc[] = {
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

  D3D12_ROOT_PARAMETER1 probe_root_parameters[] = {
    RootConstants{ .Register = 0, .SizeBytes = sizeof( DrawList::PerBatch ) },
    RootConstantBuffer{ .Register = 1 },
    RootConstants{ .Register = 2, .SizeBytes = sizeof( ProbeInfo ) },
  };

  ComPtr<ID3D12RootSignature> probe_root_signature = render_device->CreateRootSignature( {
      .RootParameters = probe_root_parameters,
      .StaticSamplers = probe_static_sampler_desc,
      .DebugName      = "Reflection Probe Root Signature",
  } );
  if ( not probe_root_signature ) return false;

  CD3DX12_DEPTH_STENCIL_DESC depth_stencil_desc{ D3D12_DEFAULT };
  depth_stencil_desc.DepthFunc               = D3D12_COMPARISON_FUNC_LESS;

  ComPtr<ID3D12PipelineState> probe_pipeline = render_device->CreateGraphicsPipeline( {
      .RootSignature    = probe_root_signature.Get(),
      .RTVFormats       = { &Environment::kProbeRenderTargetFormat, 1 },
      .RasterizerDesc   = Rasterizer{ .FrontFace = Rasterizer::FrontFace::kClockwise },
      .DepthStencilDesc = depth_stencil_desc,
      .AmpShaderName    = "ReflectionProbeAS.cso",
      .MeshShaderName   = "ReflectionProbeMS.cso",
      .PixelShaderName  = "ReflectionProbePS.cso",
      .DSVFormat        = Environment::kProbeDepthFormat,
      .DebugName        = "Reflection Probe Pipeline",
  } );
  if ( not probe_pipeline ) return false;

  new ( out ) Environment::Pipelines{
    .IBLRootSignature     = std::move( ibl_root_signature ),
    .EqRectToCubePipeline = std::move( eqrect_to_cube_pipeline ),
    .DiffuseIrradiance    = std::move( diffuse_irradiance_pipeline ),
    .Prefilter            = std::move( prefilter_pipeline ),
    .BrdfLUT              = std::move( brdf_lut_pipeline ),
    .ProbeRootSignature   = std::move( probe_root_signature ),
    .ProbePipeline        = std::move( probe_pipeline ),
  };

  return true;
};

Texture GenerateDiffuseIrradiance( EnvContext const& context, Texture const& skybox )
{
  uint32_t constexpr kThreadGroupX = 16;
  uint32_t constexpr kThreadGroupY = 16;
  uint32_t constexpr kThreadGroupZ = 1;

  // Create texture
  auto diffuse_irradiance = context.RenderDevice->CreateTextureCube( {
      .Format      = DXGI_FORMAT_R11G11B10_FLOAT,
      .Side        = Environment::kDiffuseCubeSide,
      .Type        = TextureType::kSampled,
      .IsReadWrite = true,
      .MipLevels   = MipLevels::kBase,
      .InitState   = D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
  } );
  diffuse_irradiance.SetName( L"Diffuse Irradiance Map" );

  EnvParams const diffuse_irradiance_root_constant{
    .InputTexture  = skybox,
    .OutputTexture = diffuse_irradiance,
    .CubeSide      = Environment::kDiffuseCubeSide,
  };

  context.CommandList->SetComputeRootSignature( context.Pipelines->IBLRootSignature.Get() );
  context.CommandList->SetPipelineState( context.Pipelines->DiffuseIrradiance.Get() );
  context.CommandList->BindComputeResources( 0, diffuse_irradiance_root_constant );
  context.CommandList->Dispatch( {
      .X = Environment::kDiffuseCubeSide / kThreadGroupX,
      .Y = Environment::kDiffuseCubeSide / kThreadGroupY,
      .Z = 6 / kThreadGroupZ,
  } );

  return diffuse_irradiance;
};

Texture GeneratePrefilter( EnvContext const& context, Texture const& skybox )
{
  size_t constexpr static kThreadGroupX = 16;
  size_t constexpr static kThreadGroupY = 16;
  size_t constexpr static kThreadGroupZ = 1;

  // Create texture
  auto prefilter = context.RenderDevice->CreateTextureCube( {
      .Format      = DXGI_FORMAT_R11G11B10_FLOAT,
      .Side        = Environment::kPrefilterCubeSide,
      .Type        = TextureType::kSampled,
      .IsReadWrite = true,
      .MipLevels   = Environment::kPrefilterMaxLoD + 1, // accounting for mip0
      .InitState   = D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
  } );
  prefilter.SetName( L"Prefiltered Cube" );

  PrefilterParams prefilter_constant{
    .Skybox        = skybox,
    .SkyboxSide    = Environment::kEnvCubeSide,
    .OutputTexture = prefilter,
    .OutputSide    = Environment::kPrefilterCubeSide,
    .Roughness     = 0.0f,
  };

  ASSERT( prefilter.GetDesc().MipLevels == Environment::kPrefilterMaxLoD + 1 /* Accounting for mip0 */ );

  context.CommandList->SetComputeRootSignature( context.Pipelines->IBLRootSignature.Get() );
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

  return prefilter;
}

Texture GenerateBrdfLUT( EnvContext const& context )
{
  size_t constexpr static kThreadGroupX = 16;
  size_t constexpr static kThreadGroupY = 16;

  // Create BRDF LUT texture.
  auto brdf_lut = context.RenderDevice->CreateTexture2D( {
      .Format      = DXGI_FORMAT_R16G16_FLOAT,
      .Width       = Environment::kBrdfLUTSize,
      .Height      = Environment::kBrdfLUTSize,
      .Type        = TextureType::kSampled,
      .IsReadWrite = true,
      .MipLevels   = MipLevels::kBase,
      .InitState   = D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
  } );
  brdf_lut.SetName( L"BRDF LUT" );

  BrdfLUTParams const brdf_lut_constant{
    .OutputTexture = brdf_lut,
    .Width         = Environment::kBrdfLUTSize,
    .Height        = Environment::kBrdfLUTSize,
  };

  context.CommandList->SetComputeRootSignature( context.Pipelines->IBLRootSignature.Get() );
  context.CommandList->SetPipelineState( context.Pipelines->BrdfLUT.Get() );
  context.CommandList->BindComputeResources( 0, brdf_lut_constant );
  context.CommandList->Dispatch( {
      .X = Environment::kBrdfLUTSize / kThreadGroupX,
      .Y = Environment::kBrdfLUTSize / kThreadGroupY,
  } );

  return brdf_lut;
}

Environment::IBLEnvironment CreateIBLEnvironment( EnvContext const& context, Texture skybox )
{
  return {
    skybox,
    GenerateDiffuseIrradiance( context, skybox ),
    GeneratePrefilter( context, skybox ),
  };
}
} // namespace
} // namespace Ember

Ember::Environment::Environment(
    RenderDevice* render_device, World* world, IBLEnvironment ibl, Pipelines pipelines, Texture brdf_lut )
  : m_RenderDevice{ render_device }
  , m_World{ world }
  , m_FallbackIBL{ std::move( ibl ) }
  , m_Pipelines{ std::move( pipelines ) }
  , m_BrdfLUT{ std::move( brdf_lut ) }
  , m_Repr{
    .Skybox                = m_FallbackIBL.Skybox.GetSRVHandle(),
    .DiffuseIrradiance     = m_FallbackIBL.DiffuseIrradiance.GetSRVHandle(),
    .Prefilter             = m_FallbackIBL.Prefilter.GetSRVHandle(),
    .BrdfLUT               = m_BrdfLUT.GetSRVHandle(),
    .ReflectionProbes      = {},
    .CellProbeMap          = {},
    .CellProbeMapSlotCount = 0,
    .CellSize              = 1.0f,
  }
{
  _ = world->GetECS().component<ReflectionProbe>().member<float>( "radius", 0, offsetof( ReflectionProbe, Radius ) );
}

Ember::Environment::GpuRepr const& Ember::Environment::Repr() const
{
  return m_Repr;
}

bool Ember::Environment::Bake(
    CommandList* command_list, MipMapGenerator* mipmapper, FrameGraphBlackboard const& blackboard )
{
  if ( not m_RenderDevice ) return false;

  m_ReflectionProbeTextures.clear();

  DirectX::BoundingBox   total_bb;

  std::vector<ProbeInfo> probe_infos;
  m_World->GetECS().each(
      [&]( ReflectionProbe const& probe, WorldTransform const& world_transform )
      {
        auto const position = world_transform.GetTranslation();
        probe_infos.push_back( {
            .Position      = position,
            .CaptureRadius = probe.Radius,
        } );

        DirectX::BoundingBox bb;
        DirectX::BoundingBox::CreateFromSphere( bb, { position, probe.Radius } );
        DirectX::BoundingBox::CreateMerged( total_bb, total_bb, bb );
      } );

  if ( probe_infos.empty() ) return true;

  DirectX::XMFLOAT3 min_lattice, max_lattice;
  DirectX::XMStoreFloat3(
      &min_lattice,
      DirectX::XMVectorFloor( DirectX::XMVectorSubtract(
          DirectX::XMLoadFloat3( &total_bb.Center ), DirectX::XMLoadFloat3( &total_bb.Extents ) ) ) );
  DirectX::XMStoreFloat3(
      &max_lattice,
      DirectX::XMVectorCeiling( DirectX::XMVectorAdd(
          DirectX::XMLoadFloat3( &total_bb.Center ), DirectX::XMLoadFloat3( &total_bb.Extents ) ) ) );

  SpatialHashMap probe_hashmap{ 1.0f, 1024 };

  // TODO: Replace this with some BVH if this becomes a bottleneck.
  for ( int z = ( int )min_lattice.z; z < ( int )max_lattice.z; z++ )
  {
    for ( int y = ( int )min_lattice.y; y < ( int )max_lattice.y; y++ )
    {
      for ( int x = ( int )min_lattice.x; x < ( int )max_lattice.x; x++ )
      {
        DirectX::XMFLOAT3 position = { ( float )x + 0.5f, ( float )y + 0.5f, ( float )z + 0.5f };

        if ( probe_hashmap.Contains( position ) ) continue;

        float    min_dist = std::numeric_limits<float>::infinity();
        uint32_t min_idx  = UINT32_MAX;

        for ( uint32_t i = 0; i < probe_infos.size(); i++ )
        {
          auto const& probe_info = probe_infos[i];
          if ( probe_info.CaptureRadius <= 0.0f ) continue;

          auto const dist = DirectX::XMVectorGetX( DirectX::XMVector3LengthSq( DirectX::XMVectorSubtract(
              DirectX::XMLoadFloat3( &probe_info.Position ), DirectX::XMLoadFloat3( &position ) ) ) );
          if ( dist > probe_info.CaptureRadius * probe_info.CaptureRadius ) continue;

          if ( dist < min_dist )
          {
            min_dist = dist;
            min_idx  = i;
          }
        }

        probe_hashmap.Put( position, min_idx );
      }
    }
  }

  auto const spatial_hashmap_control     = probe_hashmap.GetControlStore();
  auto const spatial_hashmap_indirection = probe_hashmap.GetIndirectionStore();

  auto const total_size = ByteSizeOf( spatial_hashmap_control ) + ByteSizeOf( spatial_hashmap_indirection );
  if ( m_CellProbeMapBuffer.GetSize() < total_size or m_CellProbeMapBuffer.GetSize() > 2 * total_size )
  {
    m_CellProbeMapBuffer = m_RenderDevice->CreateRawStorageBuffer( total_size );
    m_CellProbeMapBuffer.SetName( L"Probe Index Buffer" );
  }

  // Write
  {
    size_t size = ByteSizeOf( spatial_hashmap_control );
    m_CellProbeMapBuffer.Write( 0, size, DataOf( spatial_hashmap_control ) );

    m_CellProbeMapBuffer.Write(
        size, ByteSizeOf( spatial_hashmap_indirection ), DataOf( spatial_hashmap_indirection ) );
  }

  if ( probe_infos.empty() ) return true;

  Texture depth_tex = m_RenderDevice->CreateTextureCube( {
      .Format    = kProbeDepthFormat,
      .Side      = kEnvCubeSide,
      .Type      = TextureType::kDepthStencil,
      .MipLevels = MipLevels::kBase,
      .InitState = D3D12_RESOURCE_STATE_DEPTH_WRITE,
  } );

  wchar_t name_buf[32];

  auto    probe_skyboxes =
      std::views::repeat(
          TexCubeDesc{
              .Format      = kProbeRenderTargetFormat,
              .Side        = kEnvCubeSide,
              .Type        = TextureType::kRenderTarget,
              .IsReadWrite = true,
              .MipLevels   = MipLevels::kAuto,
              .InitState   = D3D12_RESOURCE_STATE_COPY_DEST,
          },
          probe_infos.size() ) |
      std::views::transform( [&]( auto const& desc ) { return m_RenderDevice->CreateTextureCube( desc ); } ) |
      std::ranges::to<std::vector>();

  auto fallback_skybox_state = m_FallbackIBL.Skybox.GetCurrentState();
  command_list->ResourceBarrier( CD3DX12_RESOURCE_BARRIER::Transition(
      m_FallbackIBL.Skybox.GetTexture(), fallback_skybox_state, D3D12_RESOURCE_STATE_COPY_SOURCE ) );
  m_FallbackIBL.Skybox.SetCurrentState( D3D12_RESOURCE_STATE_COPY_SOURCE );

  {
    // TODO: This will be super expensive without some 'serious' culling.
    ZoneScopedN( "Reflection Probe Capture" );
    PIXScopedEvent( command_list->Get(), PIX_COLOR_DEFAULT, "Reflection Probe Capture" );

    // Copy Skybox to each probe skybox before rendering over them.
    for ( int i = 0; auto& skybox : probe_skyboxes )
    {
      skybox.SetName( FormatTo( name_buf, L"Probe Skybox {}", i++ ) );

      command_list->CopyResource( skybox.GetTexture(), m_FallbackIBL.Skybox.GetTexture() );
    }

    command_list->ResourceBarrier( CD3DX12_RESOURCE_BARRIER::Transition(
        m_FallbackIBL.Skybox.GetTexture(), m_FallbackIBL.Skybox.GetCurrentState(), fallback_skybox_state ) );
    m_FallbackIBL.Skybox.SetCurrentState( fallback_skybox_state );

    std::vector<D3D12_RESOURCE_BARRIER> barriers( probe_skyboxes.size() );

    for ( int i = 0; auto& skybox : probe_skyboxes )
    {
      barriers[i] = CD3DX12_RESOURCE_BARRIER::Transition(
          skybox.GetTexture(), skybox.GetCurrentState(), D3D12_RESOURCE_STATE_RENDER_TARGET );
      skybox.SetCurrentState( D3D12_RESOURCE_STATE_RENDER_TARGET );

      i++;
    }
    command_list->ResourceBarrier( barriers );

    auto const& [constants_buf] = blackboard.get<FrameConstants>();
    auto const& batch           = blackboard.get<DrawList::Batches>().Opaque();

    command_list->Track( depth_tex.GetTexture() );

    for ( int i = 0; i < probe_skyboxes.size(); i++ )
    {
      ProbeInfo const& probe_info = probe_infos[i];
      Texture const&   skybox     = probe_skyboxes[i];

      command_list->Track( skybox.GetTexture() );
      command_list->ClearDepthStencilView( depth_tex.GetTexture(), D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0 );
      command_list->OMSetRenderTargets( 1, &skybox, &depth_tex );

      command_list->SetGraphicsRootSignature( m_Pipelines.ProbeRootSignature.Get() );
      command_list->SetPipelineState( m_Pipelines.ProbePipeline.Get() );
      command_list->RSSetScissorViewport( kEnvCubeSide, kEnvCubeSide );
      command_list->SetGraphicsRootConstants( 0, batch );
      command_list->SetGraphicsRootConstantBuffer( 1, constants_buf );
      command_list->SetGraphicsRootConstants( 2, probe_info );
      command_list->DispatchMesh( { .X = batch.CommandsCount } );
    }

    // Transition everything to D3D12_RESOURCE_STATE_COPY_DEST all at once.
    // This way we group the transitions and MipMap will skip the transitions.
    for ( int i = 0; auto& skybox : probe_skyboxes )
    {
      barriers[i] = CD3DX12_RESOURCE_BARRIER::Transition(
          skybox.GetTexture(), skybox.GetCurrentState(), D3D12_RESOURCE_STATE_COPY_DEST );
      skybox.SetCurrentState( D3D12_RESOURCE_STATE_COPY_DEST );

      i++;
    }
    command_list->ResourceBarrier( barriers );

    for ( auto& skybox : probe_skyboxes )
    {
      if ( not mipmapper->TryGenerateMipMapCube( command_list, &skybox ) ) return false;
    }

    // Batch transforms to
    for ( int i = 0; auto& skybox : probe_skyboxes )
    {
      barriers[i] = CD3DX12_RESOURCE_BARRIER::Transition(
          skybox.GetTexture(), skybox.GetCurrentState(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS );
      skybox.SetCurrentState( D3D12_RESOURCE_STATE_UNORDERED_ACCESS );

      i++;
    }
    command_list->ResourceBarrier( barriers );
  }

  EnvContext const context = {
    .RenderDevice = m_RenderDevice,
    .CommandList  = command_list,
    .Pipelines    = &m_Pipelines,
  };

  {
    // TODO: This will be super expensive without some 'serious' culling.
    ZoneScopedN( "Generate IBL Maps" );
    PIXScopedEvent( command_list->Get(), PIX_COLOR_DEFAULT, "Generate IBL Maps" );

    m_ReflectionProbeTextures = probe_skyboxes | std::views::enumerate |
                                std::views::transform(
                                    [&]( std::pair<size_t, Texture> const& skybox )
                                    {
                                      auto prefilter = GeneratePrefilter( context, skybox.second );
                                      prefilter.SetName( FormatTo( name_buf, L"Probe Prefilter {}", skybox.first ) );
                                      return prefilter;
                                    } ) |
                                std::ranges::to<std::vector>();
  }

  std::vector<ReflectionProbeRepr> probe_reprs;
  probe_reprs.reserve( m_ReflectionProbeTextures.size() );

  for ( int i = 0; i < m_ReflectionProbeTextures.size(); i++ )
  {
    auto const& [position, radius] = probe_infos[i];
    probe_reprs.push_back( {
        .Position  = { position.x, position.y, position.z },
        .Radius    = radius,
        .Prefilter = m_ReflectionProbeTextures[i].GetSRVHandle(),
    } );
  }

  // Commit to GPU
  uint64_t const req_buffer_size = ByteSizeOf( probe_reprs );
  if ( auto const current_buffer_size = m_ReflectionProbeBuffer.GetSize();
       req_buffer_size > current_buffer_size or req_buffer_size < ( current_buffer_size / 2 ) ) // Arb but feels right.
  {
    m_ReflectionProbeBuffer =
        m_RenderDevice->CreateStorageBuffer( ( uint32_t )req_buffer_size, StrideOf( probe_reprs ) );
    m_ReflectionProbeBuffer.SetName( L"IBL Probe Data" );
  }
  m_ReflectionProbeBuffer.Write( 0, req_buffer_size, DataOf( probe_reprs ) );

  m_Repr.ReflectionProbes      = m_ReflectionProbeBuffer.GetSRVHandle();
  m_Repr.CellProbeMap          = m_CellProbeMapBuffer.GetSRVHandle();
  m_Repr.CellProbeMapSlotCount = ( uint32_t )probe_hashmap.GetSlotCount();
  m_Repr.CellSize              = probe_hashmap.GetCellSize();

  return true;
}

bool Ember::Environment::TryLoadFromFile( Environment* env, LoadFromFile const& args )
{
  RenderDevice*  render_device  = args.RenderDevice;
  TextureLoader* texture_loader = args.TextureLoader;
  char const*    env_map_file   = args.FileName;

  Texture        environment;
  if ( not texture_loader->TryLoadTexture( &environment, env_map_file ) ) return false;
  render_device->WaitOn( texture_loader->EndBatch() );

  return TryLoadFromEqRect(
      env, { render_device, args.World, texture_loader->GetMipMapper(), std::move( environment ) } );
}

bool Ember::Environment::TryLoadFromEqRect( Environment* env, LoadFromEqRect const& args )
{
  RenderDevice*    render_device = args.RenderDevice;
  World*           world         = args.World;
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

  Texture        skybox   = GenerateSkybox( context, mip_mapper, environment );
  Texture        brdf_lut = GenerateBrdfLUT( context );
  IBLEnvironment ibl      = CreateIBLEnvironment( context, skybox );

  Queue::Receipt receipt  = queue.Submit( std::move( command_list ) );
  queue.WaitOn( receipt );

  new ( env ) Environment{
    render_device, world, std::move( ibl ), std::move( pipelines ), std::move( brdf_lut ),
  };

  return true;
}

bool Ember::Environment::TryLoadFromCube( Environment* env, LoadFromCube const& args )
{
  RenderDevice* render_device = args.RenderDevice;
  World*        world         = args.World;

  Queue         queue         = render_device->CreateQueue( D3D12_COMMAND_LIST_TYPE_COMPUTE );
  auto          command_list  = queue.GetCommandList();

  Pipelines     pipelines;
  if ( not CreatePipelines( &pipelines, render_device ) ) return false;

  EnvContext const context{
    .RenderDevice = render_device,
    .CommandList  = &command_list,
    .Pipelines    = &pipelines,
  };

  Texture        skybox   = args.CubeTexture;
  Texture        brdf_lut = GenerateBrdfLUT( context );
  IBLEnvironment ibl      = CreateIBLEnvironment( context, skybox );

  Queue::Receipt receipt  = queue.Submit( std::move( command_list ) );
  queue.WaitOn( receipt );

  new ( env ) Environment{
    render_device, world, std::move( ibl ), std::move( pipelines ), std::move( brdf_lut ),
  };

  return true;
}
