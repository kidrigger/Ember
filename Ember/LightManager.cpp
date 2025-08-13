#include "LightManager.hpp"

#include "OmniLightManager.hpp"

Ember::LightManager::LightManager(
    std::unique_ptr<Internal::OmniLightManager>      omni_light_manager,
    std::unique_ptr<Internal::DirectionLightManager> dir_light_manager )
  : m_OmniLightManager{ std::move( omni_light_manager ) }, m_DirLightManager{ std::move( dir_light_manager ) }
{}

void Ember::LightManager::Create( LightManager* light_manager, RenderDevice* render_device, uint32_t const num_frames )
{
  auto omni_light_manager = std::make_unique_for_overwrite<Internal::OmniLightManager>();
  Internal::OmniLightManager::Create( omni_light_manager.get(), render_device, num_frames );

  auto dir_light_manager = std::make_unique_for_overwrite<Internal::DirectionLightManager>();
  Internal::DirectionLightManager::Create( dir_light_manager.get(), render_device, num_frames );

  new ( light_manager ) LightManager{ std::move( omni_light_manager ), std::move( dir_light_manager ) };
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

Ember::DirLightHandle Ember::LightManager::AddDirLight( DirectX::XMFLOAT3 direction, Color32 color, float intensity )
{
  return m_DirLightManager->AddDirLight( direction, color, intensity );
}

void Ember::LightManager::Free( OmniLightHandle const omni_light_handle )
{
  m_OmniLightManager->Free( omni_light_handle );
}

void Ember::LightManager::Free( DirLightHandle const dir_light_handle )
{
  m_DirLightManager->Free( dir_light_handle );
}

std::tuple<Ember::SRVHandle, Ember::SRVHandle> Ember::LightManager::PrepareFrame( uint32_t const frame_index ) const
{
  return { m_OmniLightManager->PrepareFrame( frame_index ), m_DirLightManager->PrepareFrame( frame_index ) };
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

void Ember::LightManager::RenderAllShadows(
    ID3D12GraphicsCommandList*      command_list,
    World const&                    world,
    RenderTargetManager const&      rtm,
    DirectX::BoundingFrustum const& camera_frustum ) const
{
  m_OmniLightManager->RenderAllShadows( command_list, world, rtm, camera_frustum );
}
