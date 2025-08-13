#include "DirectionLightManager.hpp"

#include "RenderDevice.hpp"
#include "Util/DataUtil.hpp"

void Ember::Internal::DirectionLightManager::SetDirty()
{
  m_DirtyFrames = ( uint8_t )m_DataBuffers.size();
}

void Ember::Internal::DirectionLightManager::SwapTrueLocations( uint16_t const first, uint16_t const second )
{
  if ( first == second ) return;

  auto const indirection_first  = std::ranges::find( m_IndirectionMap, first );
  auto const indirection_second = std::ranges::find( m_IndirectionMap, second );
  ASSERT( indirection_first != std::ranges::end( m_IndirectionMap ) );
  ASSERT( indirection_second != std::ranges::end( m_IndirectionMap ) );
  *indirection_first  = second;
  *indirection_second = first;

  std::swap( m_LightData[first], m_LightData[second] );
}

Ember::Internal::DirectionLightManager::DirectionLightManager(
    RenderDevice* const         render_device,
    Buffer                      shadow_projection_buffer,
    std::vector<Buffer>         data_buffers,
    ComPtr<ID3D12PipelineState> pipeline,
    ComPtr<ID3D12RootSignature> root_signature )
  : m_RenderDevice{ render_device }
  , m_ShadowProjectionBuffer{ std::move( shadow_projection_buffer ) }
  , m_RootSignature{ std::move( root_signature ) }
  , m_Pipeline{ std::move( pipeline ) }
  , m_DataBuffers{ std::move( data_buffers ) }
{}

void Ember::Internal::DirectionLightManager::Create(
    DirectionLightManager* light_manager, RenderDevice* render_device, uint32_t const num_frames )
{

  std::vector<Buffer> data_buffers;
  for ( uint32_t i = 0; i < num_frames; i++ )
  {
    data_buffers.push_back(
        render_device->CreateStorageBuffer( sizeof( DirLight ) * kMaxDirLights, sizeof( DirLight ) ) );
  }

  new ( light_manager ) DirectionLightManager{
    render_device, {}, data_buffers, {}, {},
  };
}

Ember::DirLightHandle Ember::Internal::DirectionLightManager::AddDirLight(
    DirectX::XMFLOAT3 const direction, Color32 const color, float const intensity )
{
  ASSERT_M( m_TotalLightCount < kMaxDirLights, "All free locs exhausted" );

  uint16_t const true_index    = m_TotalLightCount;
  uint16_t const index         = m_IndirectionFreeHead;

  m_IndirectionFreeHead        = m_IndirectionMap[index];
  m_IndirectionMap[index]      = true_index;

  uint16_t const    generation = m_HandleGeneration[index];

  DirectX::XMFLOAT3 direction_intensity;
  XMStoreFloat3(
      &direction_intensity,
      DirectX::XMVectorScale( DirectX::XMVector3Normalize( XMLoadFloat3( &direction ) ), intensity ) );

  m_LightData[true_index] = DirLight{
    .DirectionIntensity = direction_intensity,
    .Color              = color,
  };

  m_TotalLightCount++;

  SetDirty();

  return DirLightHandle{ index, generation };
}

void Ember::Internal::DirectionLightManager::Free( DirLightHandle dir_light_handle )
{
  uint16_t const index      = dir_light_handle.GetIndex();
  uint16_t const generation = dir_light_handle.GetGeneration();

  ASSERT( m_HandleGeneration[index] == generation );
  m_HandleGeneration[index]++;

  uint16_t const true_index          = m_IndirectionMap[index];

  uint16_t const last_omni_light_idx = m_TotalLightCount - 1;

  if ( true_index < m_ShadowingLightCount )
  {
    uint16_t const last_shadowing_idx = m_ShadowingLightCount - 1;
    // To pack all shadow casters together
    // Swap with last shadow caster
    SwapTrueLocations( true_index, last_shadowing_idx );
    // Then swap with last light
    SwapTrueLocations( last_shadowing_idx, last_omni_light_idx );

    // TODO:
    // FreeOmniShadow( omni_light_handle );
    // m_LightData[last_omni_light_idx].ShadowMap = {};
  }
  else
  {
    // True index non-shadow casting.
    SwapTrueLocations( true_index, last_omni_light_idx );
  }

  m_TotalLightCount--;

  SetDirty();

  if ( m_HandleGeneration[index] == UINT16_MAX ) return; // Handle no longer usable.

  m_IndirectionMap[index] = m_IndirectionFreeHead;
  m_IndirectionFreeHead   = index;
}

Ember::SRVHandle Ember::Internal::DirectionLightManager::PrepareFrame( uint32_t const frame_index )
{
  if ( m_DirtyFrames )
  {
    m_DataBuffers[frame_index].Write( 0, sizeof( DirLight ) * m_TotalLightCount, DataOf( m_LightData ) );
    m_DirtyFrames--;
  }

  return m_DataBuffers[frame_index].GetSRVHandle();
}

uint16_t Ember::Internal::DirectionLightManager::GetDirLightCount() const
{
  return m_TotalLightCount;
}

uint16_t Ember::Internal::DirectionLightManager::GetShadowingDirLightCount() const
{
  return m_ShadowingLightCount;
}
