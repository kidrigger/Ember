#pragma once

#include <map>

#include <Graphics/RenderDevice.hpp>
#include <Util/DirectXHeaders.hpp>
#include <Util/FlatMap.hpp>
#include "Color.hpp"
#include "LightHandle.hpp"
#include "Scene.hpp"

namespace Ember
{
struct OmniLight;

class Camera;
struct RenderCommandQueue;

namespace Internal
{

class OmniLightManager
{
  using QueryLights                               = flecs::query<WorldTransform const, OmniLight const>;

  uint32_t constexpr static kOmniShadowResolution = 1024;

  struct OmniLightRepr
  {
    DirectX::XMFLOAT3 Position{ 0.0f, 0.0f, 0.0f }; // 12
    float             Range{ -1.0f };               // 16
    Color32           Color;                        // 20
    float             Intensity{ 1.0f };            // 24
    float             Attenuation{ 1.0f };          // 28
    SRVHandle         ShadowMap;                    // 32
  };
  static_assert( sizeof( OmniLightRepr ) % 16 == 0 );

  RenderDevice*               m_RenderDevice{ nullptr };
  World*                      m_World{ nullptr };
  Buffer                      m_ShadowProjectionBuffer;
  ComPtr<ID3D12RootSignature> m_RootSignature;
  ComPtr<ID3D12PipelineState> m_Pipeline;
  std::vector<OmniLightRepr>  m_LightData;
  std::vector<Buffer>         m_DataBuffers;
  std::vector<Texture>        m_ActiveShadows;
  QueryLights                 m_LightQuery;
  QueryLights                 m_ShadowLightQuery;
  uint32_t                    m_AllocatedShadows{ 0 };
  uint32_t                    m_TotalLightCount{ 0 };
  uint32_t                    m_ShadowingLightCount{ 0 };

  SRVHandle                   AllocateOmniShadow();
  void                        ClearShadows();

  static float                CalculateRange( Color32 color, float intensity );

public:
  OmniLightManager() = default;
  explicit OmniLightManager(
      RenderDevice*               render_device,
      World*                      world,
      Buffer                      shadow_projection_buffer,
      std::vector<Buffer>         buffers,
      ComPtr<ID3D12PipelineState> shadow_pipeline,
      ComPtr<ID3D12RootSignature> shadow_root_signature );

  static void Create( OmniLightManager* light_manager, RenderDevice* render_device, World* world, uint32_t num_frames );

  LightInfo   PrepareFrame( uint32_t frame_index );

  //
  void RenderAllShadows( CommandList* command_list, DrawList::Batches const& draw_list, Camera const& camera );

  void RenderOmniShadow( CommandList* command_list, DrawList::Batches const& draw_list, uint32_t light_index ) const;
};
} // namespace Internal

struct OmniLight
{
  float constexpr static kRangeAuto = -1.0f;

  Color32 Color{ Color32::White() };
  float   Range{ kRangeAuto };
  float   Intensity{ 1.0f };
};

} // namespace Ember
