#pragma once

#include <memory_resource>

#include "LightHandle.hpp"
#include "Scene.hpp"
#include "Util/FlatMap.hpp"

namespace Ember
{
struct SpotLight;
class RenderTargetManager;
class Camera;
class RenderDevice;

namespace Internal
{

class SpotLightManager
{
  using QueryLights                               = flecs::query<WorldTransform const, SpotLight const>;

  uint32_t constexpr static kSpotShadowResolution = 1024;

  struct SpotLightRepr
  {
    DirectX::XMFLOAT4X4 LightMatrix;                    // 112
    DirectX::XMFLOAT3   Position{ 0.0f, 0.0f, 0.0f };   // 12
    float               Range;                          // 16
    DirectX::XMFLOAT3   Direction{ 0.0f, 0.0f, -1.0f }; // 28
    Color32             Color;                          // 32
    float               Intensity{ 1.0f };              // 36
    float               ConeInnerCutoff{ 1.0f };        // 40
    float               ConeOuterCutoff{ 1.0f };        // 44
    SRVHandle           ShadowMap;                      // 48
  };
  static_assert( sizeof( SpotLightRepr ) % 16 == 0 );

  RenderDevice*               m_RenderDevice{ nullptr };
  World*                      m_World{ nullptr };
  std::vector<SpotLightRepr>  m_LightData;
  ComPtr<ID3D12RootSignature> m_RootSignature;
  ComPtr<ID3D12PipelineState> m_Pipeline;
  std::vector<Buffer>         m_DataBuffers;
  std::vector<Texture>        m_ActiveShadows;
  QueryLights                 m_ShadowLightQuery;
  QueryLights                 m_LightQuery;
  uint32_t                    m_AllocatedShadows;
  uint32_t                    m_TotalLightCount{ 0 };
  uint32_t                    m_ShadowingLightCount{ 0 };

  SRVHandle                   AllocateSpotShadow();
  void                        ClearShadows();

  static float                CalculateRange( Color32 color, float intensity );

public:
  SpotLightManager() = default;
  explicit SpotLightManager(
      RenderDevice*               render_device,
      World*                      world,
      std::vector<Buffer>         data_buffers,
      ComPtr<ID3D12PipelineState> shadow_pipeline,
      ComPtr<ID3D12RootSignature> shadow_root_signature );

  static void Create( SpotLightManager* light_manager, RenderDevice* render_device, World* world, uint32_t num_frames );

  LightInfo   PrepareFrame( uint32_t frame_index );

  //
  void RenderAllShadows(
      ID3D12GraphicsCommandList6* command_list,
      DrawList::Batches const&    draw_list,
      RenderTargetManager const&  rtm,
      Camera const&               camera,
      uint32_t                    frame_idx );

  void RenderSpotShadow(
      ID3D12GraphicsCommandList6* command_list,
      DrawList::Batches const&    draw_list,
      RenderTargetManager const&  rtm,
      uint32_t                    spot_light_index,
      uint32_t                    frame_idx ) const;
};

} // namespace Internal

struct SpotLight
{
  float constexpr static kRangeAuto = -1.0f;

  Color32 Color{ Color32::White() };
  float   Range{ kRangeAuto };
  float   Intensity{ 1.0f };
  float   ConeInnerHalfAngle{ DirectX::XM_PI * 0.166667f }; // 30 degrees.
  float   ConeOuterHalfAngle{ DirectX::XM_PIDIV4 };         // 45 degrees.
};

} // namespace Ember
