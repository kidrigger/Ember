#include "LightManager.hpp"

#include <imgui.h>

#include <Util/Profiling.hpp>
#include "Inspector.hpp"
#include "OmniLightManager.hpp"

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
  Internal::DirectionLightManager::Create( dir_light_manager.get(), render_device, world, num_frames );

  auto spot_light_manager = std::make_unique_for_overwrite<Internal::SpotLightManager>();
  Internal::SpotLightManager::Create( spot_light_manager.get(), render_device, world, num_frames );

  _ = world->GetECS()
          .component<Color32>()
          .member<uint8_t>( "R", 0, offsetof( Color32, R ) )
          .member<uint8_t>( "G", 0, offsetof( Color32, G ) )
          .member<uint8_t>( "B", 0, offsetof( Color32, B ) )
          .member<uint8_t>( "A", 0, offsetof( Color32, A ) )
          .set<InspectorView>( InspectorView{ []( char const* label, void* data )
                                              {
                                                Color32*          packed_color = ( Color32* )data;
                                                DirectX::XMFLOAT4 color        = packed_color->UnpackRgba();
                                                ImGui::ColorEdit4( label, ( float* )&color );
                                                *packed_color = Color32{ color };
                                              } } );
  _ = world->GetECS()
          .component<OmniLight>()
          .member<Color32>( "Color", 0, offsetof( OmniLight, Color ) )
          .member<float>( "Range", 0, offsetof( OmniLight, Range ) )
          .member<float>( "Intensity", 0, offsetof( OmniLight, Intensity ) );

  _ = world->GetECS()
          .component<SpotLight>()
          .member<Color32>( "Color", 0, offsetof( SpotLight, Color ) )
          .member<float>( "Range", 0, offsetof( SpotLight, Range ) )
          .member<float>( "Intensity", 0, offsetof( SpotLight, Intensity ) )
          .member<float>( "ConeInnerHalfAngle", 0, offsetof( SpotLight, ConeInnerHalfAngle ) )
          .range( 0.0f, DirectX::XM_PI )
          .member<float>( "ConeOuterHalfAngle", 0, offsetof( SpotLight, ConeOuterHalfAngle ) )
          .range( 0.0f, DirectX::XM_PI );

  _ = world->GetECS()
          .component<DirectionalLight>()
          .member<Color32>( "Color", 0, offsetof( DirectionalLight, Color ) )
          .member<float>( "Intensity", 0, offsetof( DirectionalLight, Intensity ) )
          .member<float>( "FarPlane", 0, offsetof( DirectionalLight, FarPlane ) );

  new ( light_manager ) LightManager{
    std::move( omni_light_manager ),
    std::move( dir_light_manager ),
    std::move( spot_light_manager ),
  };
}

Ember::LightManager::GpuInfo Ember::LightManager::PrepareFrame( Camera const& camera, uint32_t const frame_index ) const
{
  return {
    .OmniLightInfo = m_OmniLightManager->PrepareFrame( frame_index ),
    .DirLightInfo  = m_DirLightManager->PrepareFrame( camera, frame_index ),
    .SpotLightInfo = m_SpotLightManager->PrepareFrame( frame_index ),
  };
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
  m_SpotLightManager->RenderAllShadows( command_list, draw_list, rtm, camera, frame_idx );
}
