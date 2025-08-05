#include "LightManager.hpp"

#include "Util/DataUtil.hpp"

Ember::LightManager::PointLightHandle::PointLightHandle( uint16_t const inner, uint16_t const generation )
  : m_Inner{ inner }, m_Generation{ generation }
{}

uint16_t Ember::LightManager::PointLightHandle::GetInner() const
{
  return m_Inner;
}

uint16_t Ember::LightManager::PointLightHandle::GetGeneration() const
{
  return m_Generation;
}

Ember::LightManager::LightManager( std::vector<Buffer> buffers )
  : m_Buffers{ std::move( buffers ) }, m_DirtyFrames{ ( uint8_t )buffers.size() }
{
  for ( uint16_t i = 0; i < kMaxPointLights; ++i )
  {
    m_IndirectionMap[i] = i + 1;
  }
  m_FreeHead = 0;
}

void Ember::LightManager::Create( LightManager* light_manager, RenderDevice* render_device, uint32_t const num_frames )
{
  std::vector<Buffer> buffers;
  buffers.reserve( num_frames );
  for ( uint32_t i = 0; i < num_frames; i++ )
  {
    buffers.push_back(
        render_device->CreateStorageBuffer( sizeof( PointLight ) * kMaxPointLights, sizeof( PointLight ) ) );
  }

  new ( light_manager ) LightManager{ std::move( buffers ) };
}

void Ember::LightManager::SetDirty()
{
  m_DirtyFrames = ( uint8_t )m_Buffers.size();
}

Ember::LightManager::PointLightHandle Ember::LightManager::AddPointLight(
    DirectX::XMFLOAT3 const position,
    float const             range,
    Color32 const           color,
    float const             intensity,
    float const             attenuation )
{
  ASSERT_M( m_PointLightCount < kMaxPointLights, "All free locs exhausted" );

  uint16_t const true_index = m_PointLightCount;
  uint16_t const index      = m_FreeHead;

  m_FreeHead                = m_IndirectionMap[index];
  m_IndirectionMap[index]   = true_index;

  uint16_t const generation = m_Generation[true_index];

  m_PointLights[true_index] = {
    .Position    = position,
    .Range       = range,
    .Color       = color,
    .Intensity   = intensity,
    .Attenuation = attenuation,
    .Padding0    = 0.0f,
  };

  m_PointLightCount++;

  SetDirty();

  return PointLightHandle{ index, generation };
}

void Ember::LightManager::Free( PointLightHandle const point_light_handle )
{
  uint16_t const index      = point_light_handle.GetInner();
  uint16_t const generation = point_light_handle.GetGeneration();

  uint16_t const true_index = m_IndirectionMap[index];

  ASSERT( m_Generation[true_index] == generation );

  m_Generation[true_index]++;

  m_PointLightCount--;
  std::swap( m_Generation[true_index], m_Generation[m_PointLightCount] );
  m_PointLights[true_index] = m_PointLights[m_PointLightCount];

  m_IndirectionMap[index]   = m_FreeHead;
  m_FreeHead                = index;

  SetDirty();
}

Ember::LightManager::FrameInfo Ember::LightManager::PrepareFrame( uint32_t const frame_index )
{
  if ( m_DirtyFrames )
  {
    m_Buffers[frame_index].Write( 0, sizeof( PointLight ) * m_PointLightCount, DataOf( m_PointLights ) );
    m_DirtyFrames--;
  }

  return { m_Buffers[frame_index].GetSRVHandle(), m_PointLightCount };
}
