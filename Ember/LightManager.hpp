#pragma once

#include <memory>

#include "BindlessHandle.hpp"
#include "Color.hpp"
#include "DirectionLightManager.hpp"
#include "LightHandle.hpp"
#include "OmniLightManager.hpp"
#include "Util/DirectXHeaders.hpp"

namespace Ember
{
class RenderDevice;
class RenderTargetManager;
class World;

class LightManager
{
  std::unique_ptr<Internal::OmniLightManager>      m_OmniLightManager;
  std::unique_ptr<Internal::DirectionLightManager> m_DirLightManager;

public:
  LightManager() = default;
  LightManager(
      std::unique_ptr<Internal::OmniLightManager>      omni_light_manager,
      std::unique_ptr<Internal::DirectionLightManager> dir_light_manager );
  static void     Create( LightManager* light_manager, RenderDevice* render_device, uint32_t num_frames );

  OmniLightHandle AddOmniLight(
      DirectX::XMFLOAT3 position, float range, Color32 color, float intensity, float attenuation = 1.0f );
  OmniLightHandle AddShadowingOmniLight(
      DirectX::XMFLOAT3 position, float range, Color32 color, float intensity, float attenuation = 1.0f );
  DirLightHandle        AddDirLight( DirectX::XMFLOAT3 direction, Color32 color, float intensity );
  Ember::DirLightHandle AddShadowingDirLight( DirectX::XMFLOAT3 direction, Color32 color, float intensity );
  void                  Free( OmniLightHandle omni_light_handle );
  void                  Free( DirLightHandle dir_light_handle );

  [[nodiscard]] std::tuple<SRVHandle, SRVHandle> PrepareFrame( uint32_t frame_index ) const;
  [[nodiscard]] uint16_t                         GetOmniLightCount() const;
  [[nodiscard]] uint16_t                         GetShadowingOmniLightCount() const;
  [[nodiscard]] uint16_t                         GetDirLightCount() const;
  [[nodiscard]] uint16_t                         GetShadowingDirLightCount() const;

  //
  void RenderAllShadows(
      ID3D12GraphicsCommandList*      command_list,
      World const&                    world,
      RenderTargetManager const&      rtm,
      DirectX::BoundingFrustum const& camera_frustum,
      uint32_t                        frame_idx ) const;
};

} // namespace Ember
