#pragma once

#include <map>
#include <queue>

#include "Buffer.hpp"
#include "Color.hpp"
#include "LightHandle.hpp"
#include "Texture.hpp"
#include "Util/DirectXHeaders.hpp"
#include "Util/FlatMap.hpp"

namespace Ember
{
class World;
class RenderTargetManager;
class RenderDevice;
namespace Internal
{

class DirectionLightManager
{
  uint16_t constexpr static kMaxDirLights        = 4;
  uint32_t constexpr static kDirShadowResolution = 4096;
  DirectX::XMVECTORF32 constexpr static kUp      = DirectX::XMVECTORF32{ 0.0f, 1.0f, 0.0f, 0.0f };
  DirectX::XMVECTORF32 constexpr static kForward = DirectX::XMVECTORF32{ 0.0f, 0.0f, 1.0f, 0.0f };
  DirectX::XMVECTORF32 constexpr static kRight   = DirectX::XMVECTORF32{ 1.0f, 0.0f, 0.0f, 0.0f };

  struct DirLight
  {
    DirectX::XMMATRIX LightSpaceMatrix;               // 16
    DirectX::XMFLOAT3 Direction{ 0.0f, -1.0f, 0.0f }; // 28
    Color32           Color;                          // 32
    float             Intensity;                      // 36
    SRVHandle         ShadowMap;                      // 40
    uint32_t          Padding[2];                     // 48
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

  SRVHandle      PrepareFrame( uint32_t frame_index );
  [[nodiscard]] uint16_t GetDirLightCount() const;
  [[nodiscard]] uint16_t GetShadowingDirLightCount() const;

  //
  void RenderAllShadows(
      ID3D12GraphicsCommandList*      command_list,
      World const&                    world,
      RenderTargetManager const&      rtm,
      DirectX::BoundingFrustum const& camera_frustum );

  void RenderDirShadow(
      ID3D12GraphicsCommandList*      command_list,
      World const&                    world,
      RenderTargetManager const&      rtm,
      DirLight*                       dir_light,
      Texture const&                  texture,
      DirectX::BoundingFrustum const& camera_frust );
};

} // namespace Internal

} // namespace Ember
