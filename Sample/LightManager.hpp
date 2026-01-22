#pragma once

#include <memory>

#include <Graphics/DeviceHandle.hpp>
#include <Util/DirectXHeaders.hpp>
#include "Color.hpp"
#include "DirectionLightManager.hpp"
#include "LightHandle.hpp"
#include "OmniLightManager.hpp"
#include "SpotLightManager.hpp"

namespace Ember
{
class RenderDevice;
class World;
class Camera;

class LightManager
{
public:
  float constexpr static kRangeAuto = -1.0f;

  struct alignas( 16 ) GpuRepr
  {
    LightInfo OmniLightInfo;
    LightInfo DirLightInfo;
    LightInfo SpotLightInfo;
    byte      Padding[12];
  };

  static_assert( sizeof( GpuRepr ) == 48 );

private:
  GpuRepr                                          m_CachedGpuRepr{};

  std::unique_ptr<Internal::OmniLightManager>      m_OmniLightManager;
  std::unique_ptr<Internal::DirectionLightManager> m_DirLightManager;
  std::unique_ptr<Internal::SpotLightManager>      m_SpotLightManager;

public:
  LightManager() = default;
  LightManager(
      std::unique_ptr<Internal::OmniLightManager>      omni_light_manager,
      std::unique_ptr<Internal::DirectionLightManager> dir_light_manager,
      std::unique_ptr<Internal::SpotLightManager>      spot_light_manager );
  static void Create( LightManager* light_manager, RenderDevice* render_device, World* world, uint32_t num_frames );

  [[nodiscard]] GpuRepr const& PrepareFrame( Camera const& camera, uint32_t frame_index );
  [[nodiscard]] GpuRepr const& GetGpuRepr() const noexcept;

  //
  void RenderAllShadows(
      CommandList*             command_list,
      DrawList::Batches const& draw_list,
      Camera const&            camera,
      Buffer const&            frame_constants,
      uint32_t                 frame_idx ) const;
};

} // namespace Ember
