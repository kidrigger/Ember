#include "SpotLightManager.hpp"

#include <tracy/Tracy.hpp>

#include "Util/DataUtil.hpp"

Ember::SRVHandle Ember::Internal::SpotLightManager::AllocateSpotShadow()
{
  if ( m_AllocatedShadows == m_ActiveShadows.size() )
  {
    m_ActiveShadows.emplace_back( m_RenderDevice->CreateTexture2D( {
        .Format    = DXGI_FORMAT_D16_UNORM,
        .Width     = kSpotShadowResolution,
        .Height    = kSpotShadowResolution,
        .Usage     = TextureUsage::kDepthSample,
        .MipLevels = 1,
        .InitState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
    } ) );

    wchar_t buf[32];
    swprintf_s( buf, L"Spot Shadow Map %u", m_AllocatedShadows );
    m_ActiveShadows.back().SetName( buf );
  }

  return m_ActiveShadows[m_AllocatedShadows++].GetSRVHandle();
}

void Ember::Internal::SpotLightManager::ClearShadows()
{
  m_AllocatedShadows = 0;
}

float Ember::Internal::SpotLightManager::CalculateRange( Color32 const color, float const intensity )
{
  auto [x, y, z]       = color.UnpackRgb();
  float const max_comp = std::max( x, std::max( y, z ) );
  return sqrt( ( max_comp * intensity ) ) * 10.0f;
}

Ember::Internal::SpotLightManager::SpotLightManager(
    RenderDevice*               render_device,
    World*                      world,
    std::vector<Buffer>         data_buffers,
    ComPtr<ID3D12PipelineState> shadow_pipeline,
    ComPtr<ID3D12RootSignature> shadow_root_signature )
  : m_RenderDevice{ render_device }
  , m_World{ world }
  , m_RootSignature{ std::move( shadow_root_signature ) }
  , m_Pipeline{ std::move( shadow_pipeline ) }
  , m_DataBuffers{ std::move( data_buffers ) }
  , m_AllocatedShadows{ 0 }
{
  m_ShadowLightQuery =
      m_World->GetECS().query_builder<WorldTransform const, SpotLight const>().with<LightShadow const>().build();
  m_LightQuery =
      m_World->GetECS().query_builder<WorldTransform const, SpotLight const>().without<LightShadow const>().build();
}

void Ember::Internal::SpotLightManager::Create(
    SpotLightManager* light_manager, RenderDevice* render_device, World* world, uint32_t const num_frames )
{
  std::vector<Buffer> data_buffers( num_frames );

  for ( uint32_t frame_index = 0; frame_index < num_frames; ++frame_index )
  {
    data_buffers[frame_index] =
        render_device->CreateStorageBuffer( 8 * sizeof( SpotLightRepr ), sizeof( SpotLightRepr ) );
    wchar_t name[] = L"Spot Light Buffer 0";
    name[18]       = L'0' + ( wchar_t )frame_index;
    data_buffers[frame_index].SetName( name );
  }

  new ( light_manager ) SpotLightManager{ render_device, world, std::move( data_buffers ), {}, {} };
}

Ember::LightInfo Ember::Internal::SpotLightManager::PrepareFrame( uint32_t const frame_index )
{
  m_LightData.clear();
  ClearShadows();

  m_ShadowLightQuery.each(
      [&]( WorldTransform const& transform, SpotLight const& light )
      {
        DirectX::XMFLOAT3 const position = transform.GetTranslation();
        float const       range = light.Range > 0.0f ? light.Range : CalculateRange( light.Color, light.Intensity );
        DirectX::XMFLOAT3 direction;
        XMStoreFloat3(
            &direction,
            DirectX::XMVector3Rotate(
                DirectX::XMVectorSet( 0.0f, 0.0f, -1.0f, 0.0f ), XMQuaternionRotationMatrix( transform.Transform ) ) );

        m_LightData.push_back( {
            .Position        = position,
            .Range           = range,
            .Direction       = direction,
            .Color           = light.Color,
            .Intensity       = light.Intensity,
            .ConeInnerCutoff = cos( light.ConeInnerHalfAngle ),
            .ConeOuterCutoff = cos( light.ConeOuterHalfAngle ),
            .ShadowMap       = AllocateSpotShadow(),
        } );
      } );

  m_ShadowingLightCount = ( uint32_t )m_LightData.size();

  m_LightQuery.each(
      [&]( WorldTransform const& transform, SpotLight const& light )
      {
        DirectX::XMFLOAT3 const position = transform.GetTranslation();
        float const       range = light.Range > 0.0f ? light.Range : CalculateRange( light.Color, light.Intensity );
        DirectX::XMFLOAT3 direction;
        XMStoreFloat3(
            &direction,
            DirectX::XMVector3Rotate(
                DirectX::XMVectorSet( 0.0f, 0.0f, -1.0f, 0.0f ), XMQuaternionRotationMatrix( transform.Transform ) ) );

        m_LightData.push_back( {
            .Position        = position,
            .Range           = range,
            .Direction       = direction,
            .Color           = light.Color,
            .Intensity       = light.Intensity,
            .ConeInnerCutoff = cos( light.ConeInnerHalfAngle ),
            .ConeOuterCutoff = cos( light.ConeOuterHalfAngle ),
            .ShadowMap       = {},
        } );
      } );

  m_TotalLightCount = ( uint32_t )m_LightData.size();

  if ( m_DataBuffers[frame_index].GetSize() < m_TotalLightCount * sizeof( SpotLightRepr ) )
  {
    m_DataBuffers[frame_index] =
        m_RenderDevice->CreateStorageBuffer( ByteSizeOf( m_LightData ), StrideOf( m_LightData ) );
    wchar_t name[] = L"Spot Light Buffer 0";
    name[18]       = L'0' + ( wchar_t )frame_index;
    m_DataBuffers[frame_index].SetName( name );
  }
  m_DataBuffers[frame_index].Write( 0, ByteSizeOf( m_LightData ), DataOf( m_LightData ) );

  return { m_DataBuffers[frame_index].GetSRVHandle(), m_ShadowingLightCount, m_TotalLightCount };
}

uint32_t Ember::Internal::SpotLightManager::GetSpotLightCount() const
{
  return m_TotalLightCount;
}

uint32_t Ember::Internal::SpotLightManager::GetShadowingSpotLightCount() const
{
  return m_ShadowingLightCount;
}
