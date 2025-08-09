#pragma once

#include "Color.hpp"
#include "RenderDevice.hpp"
#include "RenderTargetManager.hpp"
#include "Util/DirectXHeaders.hpp"

namespace Ember
{
struct RenderCommandQueue;

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
    SRVHandle         ShadowMap;                    // 32
  };

  struct ShadowInfo
  {
    SRVHandle                     ShadowHandle;
    CD3DX12_CPU_DESCRIPTOR_HANDLE RTVHandle;
    int                           Index;
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

private:
  uint32_t constexpr static kOmniShadowResolution   = 1024;
  uint32_t constexpr static kMaxPointShadowMapCount = 32; // Based on m_PointShadowMapInUseFlags being 32 bits

  PointLight                  m_PointLights[kMaxPointLights]{};
  ShadowInfo                  m_ShadowInfo[kMaxPointLights]{};
  uint16_t                    m_IndirectionMap[kMaxPointLights]{};
  uint16_t                    m_Generation[kMaxPointLights]{};
  ComPtr<ID3D12RootSignature> m_ShadowRootSignature;
  ComPtr<ID3D12PipelineState> m_ShadowPipeline;
  Buffer                      m_PointShadowProjection;
  std::vector<Buffer>         m_PointLightBuffers;
  std::vector<Texture>        m_PointShadowMaps;
  uint16_t                    m_PointShadowMapOwner[kMaxPointShadowMapCount]{};
  uint16_t                    m_FreeHead{ UINT16_MAX };
  uint16_t                    m_PointLightCount{ 0 };
  uint16_t                    m_ShadowingPointLightCount{ 0 };
  uint8_t                     m_DirtyFrames{ 0 };

  static_assert( std::numeric_limits<std::remove_cvref_t<decltype( m_IndirectionMap[0] )>>::max() > kMaxPointLights );

  void             SetDirty();
  void             SwapTrueLocations( uint16_t first, uint16_t second );

  Ember::SRVHandle AllocatePointShadowMap( uint16_t point_light_idx );
  void             FreePointShadowMap( uint16_t point_light_idx );

public:
  LightManager() = default;
  explicit LightManager(
      Buffer                      point_shadow_proj,
      std::vector<Buffer>         buffers,
      std::vector<Texture>        textures,
      ComPtr<ID3D12PipelineState> shadow_pipeline,
      ComPtr<ID3D12RootSignature> shadow_root_signature );

  static void Create(
      LightManager* light_manager, RenderDevice* render_device, uint32_t num_frames, uint32_t max_shadows = 4 );

  PointLightHandle AddPointLight(
      DirectX::XMFLOAT3 position, float range, Color32 color, float intensity, float attenuation = 1.0f );
  PointLightHandle AddShadowingPointLight(
      DirectX::XMFLOAT3 position, float range, Color32 color, float intensity, float attenuation = 1.0f );
  void                   Free( PointLightHandle point_light_handle );

  SRVHandle              PrepareFrame( uint32_t frame_index );
  CBVHandle              GetOmniProjectionsHandle() const;
  [[nodiscard]] uint16_t GetPointLightCount() const;
  [[nodiscard]] uint16_t GetShadowingPointLightCount() const;

  //
  void RenderAllShadows(
      ID3D12GraphicsCommandList* command_list, RenderCommandQueue const& rcq, RenderTargetManager const& rtm ) const;

  static void RenderOmniShadow(
      ID3D12GraphicsCommandList* command_list,
      RenderCommandQueue const&  rcq,
      RenderTargetManager const& rtm,
      PointLight const&          point_light,
      Texture const&             texture );
};

} // namespace Ember
