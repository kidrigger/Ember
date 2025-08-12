#pragma once

#include <map>

#include "Color.hpp"
#include "RenderDevice.hpp"
#include "RenderTargetManager.hpp"
#include "Scene.hpp"
#include "Util/DataUtil.hpp"
#include "Util/DirectXHeaders.hpp"

namespace Ember
{
struct RenderCommandQueue;

class OmniLightHandle
{
  uint16_t static constexpr kInvalid{ UINT16_MAX };

  uint16_t m_Inner{ kInvalid };
  uint16_t m_Generation{ kInvalid };

public:
  OmniLightHandle() = default;
  explicit OmniLightHandle( uint16_t inner, uint16_t generation );

  [[nodiscard]] uint16_t GetIndex() const;
  [[nodiscard]] uint16_t GetGeneration() const;

  std::strong_ordering   operator<=>( OmniLightHandle const& ) const;
};

class LightManager
{
  uint16_t constexpr static kMaxOmniLights        = 32;
  uint32_t constexpr static kOmniShadowResolution = 1024;

  struct OmniLight
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

  RenderDevice*                      m_RenderDevice{ nullptr };
  Buffer                             m_ShadowProjectionBuffer;
  OmniLight                          m_PointLights[kMaxOmniLights]{};
  ShadowInfo                         m_ShadowInfo[kMaxOmniLights]{};
  uint16_t                           m_IndirectionMap[kMaxOmniLights]{};
  uint16_t                           m_Generation[kMaxOmniLights]{};
  ComPtr<ID3D12RootSignature>        m_ShadowRootSignature;
  ComPtr<ID3D12PipelineState>        m_ShadowPipeline;
  std::vector<Buffer>                m_PointLightBuffers;
  std::queue<Texture>                m_OmniShadowCache;
  std::map<OmniLightHandle, Texture> m_OmniShadowsInUse;
  uint16_t                           m_FreeHead{ UINT16_MAX };
  uint16_t                           m_PointLightCount{ 0 };
  uint16_t                           m_ShadowingPointLightCount{ 0 };
  uint8_t                            m_DirtyFrames{ 0 };

  static_assert( std::numeric_limits<std::remove_cvref_t<decltype( m_IndirectionMap[0] )>>::max() > kMaxOmniLights );

  void      SetDirty();
  void      SwapTrueLocations( uint16_t first, uint16_t second );

  SRVHandle AllocateOmniShadow( OmniLightHandle point_light_idx );
  void      FreeOmniShadow( OmniLightHandle point_light_idx );

public:
  LightManager() = default;
  explicit LightManager(
      RenderDevice*               render_device,
      Buffer                      shadow_projection_buffer,
      std::vector<Buffer>         buffers,
      ComPtr<ID3D12PipelineState> shadow_pipeline,
      ComPtr<ID3D12RootSignature> shadow_root_signature );

  static void     Create( LightManager* light_manager, RenderDevice* render_device, uint32_t num_frames );

  OmniLightHandle AddOmniLight(
      DirectX::XMFLOAT3 position, float range, Color32 color, float intensity, float attenuation = 1.0f );
  OmniLightHandle AddShadowingOmniLight(
      DirectX::XMFLOAT3 position, float range, Color32 color, float intensity, float attenuation = 1.0f );
  void                   Free( OmniLightHandle point_light_handle );

  SRVHandle              PrepareFrame( uint32_t frame_index );
  [[nodiscard]] uint16_t GetPointLightCount() const;
  [[nodiscard]] uint16_t GetShadowingPointLightCount() const;

  //
  void RenderAllShadows(
      ID3D12GraphicsCommandList*      command_list,
      World const&                    world,
      RenderTargetManager const&      rtm,
      DirectX::BoundingFrustum const& camera_frustum ) const;

  void RenderOmniShadow(
      ID3D12GraphicsCommandList* command_list,
      World const&               world,
      RenderTargetManager const& rtm,
      OmniLight const&           point_light,
      Texture const&             texture ) const;
};

} // namespace Ember
