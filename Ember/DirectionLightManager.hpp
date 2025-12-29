#pragma once

#include <map>
#include <queue>

#include <Graphics/Buffer.hpp>
#include <Graphics/Texture.hpp>
#include <Util/DirectXHeaders.hpp>
#include <Util/FlatMap.hpp>
#include "Color.hpp"
#include "LightHandle.hpp"
#include "Scene.hpp"

namespace Ember
{
class World;
class RenderTargetManager;
class RenderDevice;
class Camera;

struct DirectionalLight;

namespace Internal
{

class DirectionLightManager
{
  uint32_t constexpr static kDirShadowResolution = 1024;
  uint16_t constexpr static kMaxDirLights        = 4;
  uint8_t constexpr static kNumCascades          = 6;
  float constexpr static kCascadeLambda          = 0.33f;

  // Frustums aligned with bounding boxes can cause 1 pixel gap between the cascades.
  // This overlap (in world-space) ensures the edges of the cascades overlap.
  float constexpr static kCascadeOverlap         = 0.05f;

  DirectX::XMVECTORF32 constexpr static kUp      = DirectX::XMVECTORF32{ 0.0f, 1.0f, 0.0f, 0.0f };
  DirectX::XMVECTORF32 constexpr static kForward = DirectX::XMVECTORF32{ 0.0f, 0.0f, -1.0f, 0.0f };
  DirectX::XMVECTORF32 constexpr static kRight   = DirectX::XMVECTORF32{ 1.0f, 0.0f, 0.0f, 0.0f };

  using QueryLights                              = flecs::query<WorldTransform const, DirectionalLight const>;

  struct DirLightRepr
  {
    DirectX::XMMATRIX LightSpaceMatrix[kNumCascades]; // 384
    DirectX::XMFLOAT3 Direction{ 0.0f, -1.0f, 0.0f }; // 396
    Color32           Color;                          // 400
    float             Intensity;                      // 404
    SRVHandle         ShadowMap;                      // 408
    float             FarPlane;                       // 412
    uint32_t          Padding;                        // 416
    DirectX::XMFLOAT4 CascadeSph[kNumCascades];       // 512
  };
  static_assert( sizeof( DirLightRepr ) % 16 == 0 );

  RenderDevice*               m_RenderDevice{ nullptr };
  World*                      m_World{ nullptr };
  std::vector<DirLightRepr>   m_LightData;
  ComPtr<ID3D12RootSignature> m_RootSignature;
  ComPtr<ID3D12PipelineState> m_Pipeline;
  std::vector<Buffer>         m_DataBuffers;
  std::vector<Texture>        m_ActiveShadows;
  QueryLights                 m_ShadowLightQuery;
  QueryLights                 m_LightQuery;
  uint32_t                    m_AllocatedShadows{ 0 };
  uint32_t                    m_TotalLightCount{ 0 };
  uint32_t                    m_ShadowingLightCount{ 0 };

  SRVHandle                   AllocateShadow();
  void                        ClearShadows();
  static void CalculateShadowParameters( DirectX::BoundingFrustum const& camera_frustum, DirLightRepr* dir_light );

public:
  DirectionLightManager() = default;
  DirectionLightManager(
      RenderDevice*               render_device,
      World*                      world,
      std::vector<Buffer>         data_buffers,
      ComPtr<ID3D12PipelineState> pipeline,
      ComPtr<ID3D12RootSignature> root_signature );

  static void Create(
      DirectionLightManager* light_manager, RenderDevice* render_device, World* world, uint32_t num_frames );

  LightInfo PrepareFrame( Camera const& camera, uint32_t frame_index );

  //
  void RenderAllShadows(
      ID3D12GraphicsCommandList6* command_list,
      DrawList::Batches const&    draw_info,
      RenderTargetManager const&  rtm,
      Camera const&               camera,
      uint32_t                    frame_idx );

  void RenderDirShadow(
      ID3D12GraphicsCommandList6* command_list,
      DrawList::Batches const&    draw_info,
      RenderTargetManager const&  rtm,
      Camera const&               camera,
      uint32_t                    frame_index,
      uint32_t                    light_index );
};

} // namespace Internal

struct DirectionalLight
{
  float constexpr static kRangeAuto = -1.0f;
  Color32 Color{ Color32::White() };
  float   Intensity{ 1.0f };
  float   FarPlane{ kRangeAuto };
};

struct Sun
{};

} // namespace Ember
