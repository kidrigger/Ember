#pragma once

#include <memory>

#include "BindlessHandle.hpp"
#include "Color.hpp"
#include "OmniLightHandle.hpp"
#include "OmniLightManager.hpp"
#include "Util/DirectXHeaders.hpp"

namespace Ember
{
class RenderDevice;
class RenderTargetManager;
class World;

class LightManager
{
  std::unique_ptr<Internal::OmniLightManager> m_OmniLightManager;

public:
  LightManager() = default;
  explicit LightManager( std::unique_ptr<Internal::OmniLightManager> omni_light_manager );

  static void     Create( LightManager* light_manager, RenderDevice* render_device, uint32_t num_frames );

  OmniLightHandle AddOmniLight(
      DirectX::XMFLOAT3 position, float range, Color32 color, float intensity, float attenuation = 1.0f );
  OmniLightHandle AddShadowingOmniLight(
      DirectX::XMFLOAT3 position, float range, Color32 color, float intensity, float attenuation = 1.0f );
  void                   Free( OmniLightHandle omni_light_handle );

  SRVHandle              PrepareFrame( uint32_t frame_index ) const;
  [[nodiscard]] uint16_t GetOmniLightCount() const;
  [[nodiscard]] uint16_t GetShadowingOmniLightCount() const;

  //
  void RenderAllShadows(
      ID3D12GraphicsCommandList*      command_list,
      World const&                    world,
      RenderTargetManager const&      rtm,
      DirectX::BoundingFrustum const& camera_frustum ) const;
};

} // namespace Ember
