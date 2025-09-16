#pragma once

#include <map>
#include <queue>

#include "Buffer.hpp"
#include "Color.hpp"
#include "LightHandle.hpp"
#include "Scene.hpp"
#include "Texture.hpp"
#include "Util/DirectXHeaders.hpp"
#include "Util/FlatMap.hpp"

namespace Ember
{
class World;
class RenderTargetManager;
class RenderDevice;
class Camera;

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
  DirectX::XMVECTORF32 constexpr static kForward = DirectX::XMVECTORF32{ 0.0f, 0.0f, 1.0f, 0.0f };
  DirectX::XMVECTORF32 constexpr static kRight   = DirectX::XMVECTORF32{ 1.0f, 0.0f, 0.0f, 0.0f };

  struct DirLight
  {
    DirectX::XMMATRIX LightSpaceMatrix[kNumCascades]; // 384
    DirectX::XMFLOAT3 Direction{ 0.0f, -1.0f, 0.0f }; // 396
    Color32           Color;                          // 400
    float             Intensity;                      // 404
    SRVHandle         ShadowMap;                      // 408
    uint32_t          Padding[2];                     // 416
    DirectX::XMFLOAT4 CascadeSph[kNumCascades];       // 508
  };
  static_assert( sizeof( DirLight ) % 16 == 0 );

  RenderDevice*                    m_RenderDevice{ nullptr };
  DirLight                         m_LightData[kMaxDirLights]{};
  DirectX::XMMATRIX                m_LightSpaceMatrix[kMaxDirLights]{};
  uint16_t                         m_IndirectionMap[kMaxDirLights]{};
  uint16_t                         m_HandleGeneration[kMaxDirLights]{};
  ComPtr<ID3D12RootSignature>      m_RootSignature;
  ComPtr<ID3D12PipelineState>      m_Pipeline;
  std::vector<Buffer>              m_DataBuffers;
  std::queue<Texture>              m_ShadowCache;
  FlatMap<DirLightHandle, Texture> m_ShadowsInUse;
  uint16_t                         m_IndirectionFreeHead{ UINT16_MAX };
  uint16_t                         m_TotalLightCount{ 0 };
  uint16_t                         m_ShadowingLightCount{ 0 };
  uint16_t                         m_DirtyFrames{ 0 };

  static_assert( std::numeric_limits<std::remove_cvref_t<decltype( m_IndirectionMap[0] )>>::max() > kMaxDirLights );

  void      SetDirty();
  void      SwapTrueLocations( uint16_t first, uint16_t second );

  SRVHandle AllocateDirShadow( DirLightHandle dir_light_idx );
  void      FreeDirShadow( DirLightHandle dir_light_idx );

public:
  DirectionLightManager() = default;
  DirectionLightManager(
      RenderDevice*               render_device,
      std::vector<Buffer>         data_buffers,
      ComPtr<ID3D12PipelineState> pipeline,
      ComPtr<ID3D12RootSignature> root_signature );

  static void    Create( DirectionLightManager* light_manager, RenderDevice* render_device, uint32_t num_frames );

  DirLightHandle AddDirLight( DirectX::XMFLOAT3 direction, Color32 color, float intensity );
  DirLightHandle AddShadowingDirLight( DirectX::XMFLOAT3 direction, Color32 color, float intensity );
  void           Free( DirLightHandle dir_light_handle );

  LightInfo      PrepareFrame( uint32_t frame_index );
  [[nodiscard]] uint16_t GetDirLightCount() const;
  [[nodiscard]] uint16_t GetShadowingDirLightCount() const;

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
      DirLight*                   dir_light,
      Texture const&              texture,
      Camera const&               camera,
      uint32_t                    frame_index,
      uint32_t                    light_index );
};

} // namespace Internal

} // namespace Ember
