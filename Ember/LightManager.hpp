#pragma once

#include <memory>

#include "Color.hpp"
#include "DeviceHandle.hpp"
#include "DirectionLightManager.hpp"
#include "LightHandle.hpp"
#include "OmniLightManager.hpp"
#include "SpotLightManager.hpp"
#include "Util/DirectXHeaders.hpp"

namespace Ember
{
class RenderDevice;
class RenderTargetManager;
class World;
class Camera;

class LightManager
{
  std::unique_ptr<Internal::OmniLightManager>      m_OmniLightManager;
  std::unique_ptr<Internal::DirectionLightManager> m_DirLightManager;
  std::unique_ptr<Internal::SpotLightManager>      m_SpotLightManager;

public:
  float constexpr static kRangeAuto = -1.0f;

  struct GpuInfo
  {
    LightInfo OmniLightInfo;
    LightInfo DirLightInfo;
    LightInfo SpotLightInfo;
  };

  static_assert( sizeof( GpuInfo ) == 36 );

  LightManager() = default;
  LightManager(
      std::unique_ptr<Internal::OmniLightManager>      omni_light_manager,
      std::unique_ptr<Internal::DirectionLightManager> dir_light_manager,
      std::unique_ptr<Internal::SpotLightManager>      spot_light_manager );
  static void    Create( LightManager* light_manager, RenderDevice* render_device, World* world, uint32_t num_frames );

  DirLightHandle AddDirLight( DirectX::XMFLOAT3 direction, Color32 color, float intensity );
  DirLightHandle AddShadowingDirLight( DirectX::XMFLOAT3 direction, Color32 color, float intensity );
  void           Free( DirLightHandle dir_light_handle );

  [[nodiscard]] GpuInfo  PrepareFrame( uint32_t frame_index ) const;
  [[nodiscard]] uint32_t GetOmniLightCount() const;
  [[nodiscard]] uint32_t GetShadowingOmniLightCount() const;
  [[nodiscard]] uint16_t GetDirLightCount() const;
  [[nodiscard]] uint16_t GetShadowingDirLightCount() const;

  //
  void RenderAllShadows(
      ID3D12GraphicsCommandList6* command_list,
      DrawList::Batches const&    draw_list,
      RenderTargetManager const&  rtm,
      Camera const&               camera,
      uint32_t                    frame_idx ) const;
};

} // namespace Ember
