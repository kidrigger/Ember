#pragma once

#include "Color.hpp"
#include "RenderDevice.hpp"
#include "Util/DirectXHeaders.hpp"

namespace Ember
{

class LightManager
{
  uint16_t constexpr static kMaxPointLights = 32;

  struct PointLight
  {
    DirectX::XMFLOAT3 Position{ 0.0f, 0.0f, 0.0f }; // 12
    float             Range{ -1.0f };               // 16
    Color32           Color;                        // 20
    float             Intensity{ 1.0f };            // 24
    float             Attenuation{ 1.0f };          // 28
    float             Padding0{};                   // 32
  };

public:
  class PointLightHandle
  {
    uint16_t static constexpr kInvalid{ UINT16_MAX };

    uint16_t m_Inner{ kInvalid };
    uint16_t m_Generation{ kInvalid };

  public:
    PointLightHandle() = default;
    explicit PointLightHandle( uint16_t inner, uint16_t generation );

    [[nodiscard]] uint16_t GetInner() const;
    [[nodiscard]] uint16_t GetGeneration() const;
  };

  struct FrameInfo
  {
    SRVHandle PointLightBuffer;
    uint32_t  PointLightCount;
  };

private:
  std::vector<Buffer> m_Buffers;
  uint16_t            m_FreeHead{ UINT16_MAX };
  uint16_t            m_IndirectionMap[kMaxPointLights]{};
  uint16_t            m_Generation[kMaxPointLights]{};
  PointLight          m_PointLights[kMaxPointLights]{};
  uint16_t            m_PointLightCount{ 0 };
  uint8_t             m_DirtyFrames{ 0 };

  static_assert( std::numeric_limits<std::remove_cvref_t<decltype( m_IndirectionMap[0] )>>::max() > kMaxPointLights );
  void SetDirty();

public:
  LightManager() = default;
  explicit LightManager( std::vector<Buffer> buffers );

  static void      Create( LightManager* light_manager, RenderDevice* render_device, uint32_t num_frames );

  PointLightHandle AddPointLight(
      DirectX::XMFLOAT3 position, float range, Color32 color, float intensity, float attenuation = 1.0f );
  void      Free( PointLightHandle point_light_handle );

  FrameInfo PrepareFrame( uint32_t frame_index );
};

} // namespace Ember
