#include "OmniLightManager.hpp"

#include <Util/DataUtil.hpp>
#include <Util/Profiling.hpp>
#include <Util/StringUtil.hpp>
#include <format>
#include "Camera.hpp"
#include "ModelLoader.hpp"

namespace
{
struct PackedData
{
  DirectX::XMFLOAT3 Position;
  float             FarPlane;
};

static_assert( sizeof( PackedData ) == 16 );
} // namespace

Ember::Internal::OmniLightManager::OmniLightManager(
    RenderDevice*               render_device,
    World*                      world,
    std::vector<Buffer>         light_buffers,
    ComPtr<ID3D12PipelineState> shadow_pipeline,
    ComPtr<ID3D12RootSignature> shadow_root_signature )
  : m_RenderDevice{ render_device }
  , m_World{ world }
  , m_RootSignature{ std::move( shadow_root_signature ) }
  , m_Pipeline{ std::move( shadow_pipeline ) }
  , m_DataBuffers{ std::move( light_buffers ) }
{
  m_LightQuery =
      m_World->GetECS().query_builder<WorldTransform const, OmniLight const>().without<ShadowCaster>().build();

  m_ShadowLightQuery =
      m_World->GetECS().query_builder<WorldTransform const, OmniLight const>().with<ShadowCaster>().build();
}

void Ember::Internal::OmniLightManager::Create(
    OmniLightManager* light_manager, RenderDevice* render_device, World* world, uint32_t const num_frames )
{
  std::vector<Buffer> buffers;
  buffers.reserve( num_frames );
  for ( uint32_t i = 0; i < num_frames; i++ )
  {
    buffers.push_back( render_device->CreateStorageBuffer( 8 * sizeof( OmniLightRepr ), sizeof( OmniLightRepr ) ) );
  }

  D3D12_ROOT_PARAMETER1 root_parameters[] = {
    RootConstants{ .Register = 0, .SizeBytes = sizeof( DrawList::PerBatch ) },
    RootConstants{ .Register = 1, .SizeBytes = sizeof( PackedData )         },
  };

  ComPtr<ID3D12RootSignature> shadow_root_sig = render_device->CreateRootSignature( {
      .RootParameters = root_parameters,
      .StaticSamplers = {},
      .DebugName      = "Omni Shadow Root Signature",
  } );

  ComPtr<ID3D12PipelineState> shadow_pipeline = render_device->CreateGraphicsPipeline( {
      .RootSignature = shadow_root_sig.Get(),
      .RasterizerDesc =
          Rasterizer{ .FrontFace = Rasterizer::FrontFace::kClockwise, .CullMode = Rasterizer::CullMode::kFront },
      .AmpShaderName   = "OmniShadowAS.cso",
      .MeshShaderName  = "OmniShadowMS.cso",
      .PixelShaderName = "OmniShadowPS.cso",
      .DSVFormat       = DXGI_FORMAT_D16_UNORM,
      .DebugName       = "Omni Shadow Pipeline",
  } );

  new ( light_manager ) OmniLightManager{
    render_device, world, std::move( buffers ), std::move( shadow_pipeline ), std::move( shadow_root_sig ),
  };
}

float Ember::Internal::OmniLightManager::CalculateRange( Color32 const color, float const intensity )
{
  auto [x, y, z]       = color.UnpackRgb();
  float const max_comp = std::max( x, std::max( y, z ) );
  return sqrt( ( max_comp * intensity ) ) * 10.0f;
}

Ember::SRVHandle Ember::Internal::OmniLightManager::AllocateOmniShadow()
{
  if ( m_AllocatedShadows == m_ActiveShadows.size() )
  {
    m_ActiveShadows.emplace_back( m_RenderDevice->CreateTextureCube( {
        .Format    = DXGI_FORMAT_D16_UNORM,
        .Side      = kOmniShadowResolution,
        .Type      = TextureType::kDepthStencil,
        .MipLevels = MipLevels::kBase,
        .InitState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
    } ) );

    wchar_t buf[32];
    m_ActiveShadows.back().SetName( FormatTo( buf, L"Omni Shadow Map {}", m_AllocatedShadows ) );
  }

  return m_ActiveShadows[m_AllocatedShadows++].GetSRVHandle();
}

void Ember::Internal::OmniLightManager::ClearShadows()
{
  m_AllocatedShadows = 0;
}

Ember::LightInfo Ember::Internal::OmniLightManager::PrepareFrame( uint32_t const frame_index )
{
  m_LightData.clear();
  ClearShadows();

  m_ShadowLightQuery.each(
      [&]( WorldTransform const& transform, OmniLight const& light )
      {
        DirectX::XMFLOAT3 const position = transform.GetTranslation();
        float const range = light.Range > 0.0f ? light.Range : CalculateRange( light.Color, light.Intensity );

        m_LightData.push_back( {
            .Position    = position,
            .Range       = range,
            .Color       = light.Color,
            .Intensity   = light.Intensity,
            .Attenuation = 1.0f,
            .ShadowMap   = AllocateOmniShadow(),
        } );
      } );

  m_ShadowingLightCount = ( uint32_t )m_LightData.size();

  m_LightQuery.each(
      [&]( WorldTransform const& transform, OmniLight const& light )
      {
        DirectX::XMFLOAT3 const position = transform.GetTranslation();
        float const range = light.Range > 0.0f ? light.Range : CalculateRange( light.Color, light.Intensity );

        m_LightData.push_back( {
            .Position    = position,
            .Range       = range,
            .Color       = light.Color,
            .Intensity   = light.Intensity,
            .Attenuation = 1.0f,
        } );
      } );

  m_TotalLightCount = ( uint32_t )m_LightData.size();

  if ( m_DataBuffers[frame_index].GetSize() < m_TotalLightCount * sizeof( OmniLightRepr ) )
  {
    m_DataBuffers[frame_index] =
        m_RenderDevice->CreateStorageBuffer( U32ByteSizeOf( m_LightData ), StrideOf( m_LightData ) );
    wchar_t name[] = L"Omni Light Buffer 0";
    name[18]       = L'0' + ( wchar_t )frame_index;
    m_DataBuffers[frame_index].SetName( name );
  }
  m_DataBuffers[frame_index].Write( 0, ByteSizeOf( m_LightData ), DataOf( m_LightData ) );

  return { m_DataBuffers[frame_index].GetSRVHandle(), m_ShadowingLightCount, m_TotalLightCount };
}

void Ember::Internal::OmniLightManager::RenderAllShadows(
    CommandList* command_list, DrawList::Batches const& draw_list, Camera const& camera )
{
  ZoneScoped;

  DirectX::BoundingFrustum const& camera_frustum = camera.GetLastUpdatedFrustum();

  command_list->SetGraphicsRootSignature( m_RootSignature.Get() );
  command_list->SetPipelineState( m_Pipeline.Get() );
  command_list->IASetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST );

  command_list->RSSetScissorViewport( kOmniShadowResolution, kOmniShadowResolution );

  static std::vector<D3D12_RESOURCE_BARRIER> barriers;
  barriers.resize( m_AllocatedShadows );

  std::transform(
      m_ActiveShadows.begin(),
      m_ActiveShadows.begin() + m_AllocatedShadows,
      barriers.begin(),
      []( Texture const& tex )
      {
        auto current_state = tex.GetCurrentState();
        auto next_state    = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        tex.SetCurrentState( next_state );
        return CD3DX12_RESOURCE_BARRIER::Transition( tex.GetTexture(), current_state, next_state );
      } );

  if ( not barriers.empty() ) command_list->ResourceBarrier( barriers );

  for ( uint32_t index = 0; index < m_AllocatedShadows; ++index )
  {
    ZoneScopedN( "CheckOmniShadow" );
    ZoneValue( index );

    OmniLightRepr const&    light = m_LightData[index];

    DirectX::BoundingSphere sphere_of_influence{ light.Position, light.Range };
    if ( camera_frustum.Contains( sphere_of_influence ) == DirectX::DISJOINT ) continue;

    RenderOmniShadow( command_list, draw_list, index );
  }

  std::transform(
      m_ActiveShadows.begin(),
      m_ActiveShadows.begin() + m_AllocatedShadows,
      barriers.begin(),
      []( Texture const& tex )
      {
        auto current_state = tex.GetCurrentState();
        auto next_state    = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        tex.SetCurrentState( next_state );
        return CD3DX12_RESOURCE_BARRIER::Transition( tex.GetTexture(), current_state, next_state );
      } );

  if ( not barriers.empty() ) command_list->ResourceBarrier( barriers );
}

void Ember::Internal::OmniLightManager::RenderOmniShadow(
    CommandList* command_list, DrawList::Batches const& draw_list, uint32_t const light_index ) const
{
  PIXScopedEvent( command_list->Get(), PIX_COLOR_DEFAULT, "Render Omni Shadow %u", light_index );
  ZoneScoped;

  OmniLightRepr const& omni_light = m_LightData[light_index];
  Texture const&       texture    = m_ActiveShadows[light_index];

  command_list->ClearDepthStencilView( texture.GetTexture(), D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0 );

  auto const       batch = draw_list.Opaque();

  PackedData const packed_data{
    .Position = omni_light.Position,
    .FarPlane = omni_light.Range,
  };
  command_list->SetGraphicsRootConstants( 0, batch );
  command_list->SetGraphicsRootConstants( 1, packed_data );

  command_list->OMSetRenderTargets( 0, nullptr, &texture );

  command_list->DispatchMesh( { .X = batch.CommandsCount } );
}
