#include "LightManager.hpp"

#include "OmniLightManager.hpp"
#include "Util/Profiling.hpp"

Ember::LightManager::LightManager(
    std::unique_ptr<Internal::OmniLightManager>      omni_light_manager,
    std::unique_ptr<Internal::DirectionLightManager> dir_light_manager,
    std::unique_ptr<Internal::SpotLightManager>      spot_light_manager )
  : m_OmniLightManager{ std::move( omni_light_manager ) }
  , m_DirLightManager{ std::move( dir_light_manager ) }
  , m_SpotLightManager{ std::move( spot_light_manager ) }
{}

void Ember::LightManager::Create(
    LightManager* light_manager, RenderDevice* render_device, World* world, uint32_t const num_frames )
{
  auto omni_light_manager = std::make_unique_for_overwrite<Internal::OmniLightManager>();
  Internal::OmniLightManager::Create( omni_light_manager.get(), render_device, world, num_frames );

  auto dir_light_manager = std::make_unique_for_overwrite<Internal::DirectionLightManager>();
  Internal::DirectionLightManager::Create( dir_light_manager.get(), render_device, num_frames );

  auto spot_light_manager = std::make_unique_for_overwrite<Internal::SpotLightManager>();
  Internal::SpotLightManager::Create( spot_light_manager.get(), render_device, world, num_frames );

  new ( light_manager ) LightManager{
    std::move( omni_light_manager ),
    std::move( dir_light_manager ),
    std::move( spot_light_manager ),
  };
}

Ember::OmniLightHandle Ember::LightManager::AddOmniLight(
    DirectX::XMFLOAT3 const position,
    float const             range,
    Color32 const           color,
    float const             intensity,
    float const             attenuation )
{
  return m_OmniLightManager->AddOmniLight( position, range, color, intensity, attenuation );
}

Ember::OmniLightHandle Ember::LightManager::AddShadowingOmniLight(
    DirectX::XMFLOAT3 const position,
    float const             range,
    Color32 const           color,
    float const             intensity,
    float const             attenuation )
{
  return m_OmniLightManager->AddShadowingOmniLight( position, range, color, intensity, attenuation );
}

Ember::DirLightHandle Ember::LightManager::AddDirLight(
    DirectX::XMFLOAT3 const direction, Color32 const color, float const intensity )
{
  return m_DirLightManager->AddDirLight( direction, color, intensity );
}

Ember::DirLightHandle Ember::LightManager::AddShadowingDirLight(
    DirectX::XMFLOAT3 const direction, Color32 const color, float const intensity )
{
  return m_DirLightManager->AddShadowingDirLight( direction, color, intensity );
}

void Ember::LightManager::Free( OmniLightHandle const omni_light_handle )
{
  m_OmniLightManager->Free( omni_light_handle );
}

void Ember::LightManager::Free( DirLightHandle const dir_light_handle )
{
  m_DirLightManager->Free( dir_light_handle );
}

Ember::LightManager::GpuInfo Ember::LightManager::PrepareFrame( uint32_t const frame_index ) const
{
  return {
    .OmniLightInfo = m_OmniLightManager->PrepareFrame( frame_index ),
    .DirLightInfo  = m_DirLightManager->PrepareFrame( frame_index ),
    .SpotLightInfo = m_SpotLightManager->PrepareFrame( frame_index ),
  };
}

uint16_t Ember::LightManager::GetOmniLightCount() const
{
  return m_OmniLightManager->GetOmniLightCount();
}

uint16_t Ember::LightManager::GetShadowingOmniLightCount() const
{
  return m_OmniLightManager->GetShadowingOmniLightCount();
}

uint16_t Ember::LightManager::GetDirLightCount() const
{
  return m_DirLightManager->GetDirLightCount();
}

uint16_t Ember::LightManager::GetShadowingDirLightCount() const
{
  return m_DirLightManager->GetShadowingDirLightCount();
}

void Ember::LightManager::RenderAllShadows(
    ID3D12GraphicsCommandList6* command_list,
    DrawList::Batches const&    draw_list,
    RenderTargetManager const&  rtm,
    Camera const&               camera,
    uint32_t const              frame_idx ) const
{
  PIXScopedEvent( command_list, PIX_COLOR_DEFAULT, "Render All Shadows" );
  m_OmniLightManager->RenderAllShadows( command_list, draw_list, rtm, camera );
  m_DirLightManager->RenderAllShadows( command_list, draw_list, rtm, camera, frame_idx );
}
