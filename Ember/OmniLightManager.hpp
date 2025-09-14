#pragma once

#include <map>

#include "Color.hpp"
#include "LightHandle.hpp"
#include "RenderDevice.hpp"
#include "RenderTargetManager.hpp"
#include "Scene.hpp"
#include "Util/DataUtil.hpp"
#include "Util/DirectXHeaders.hpp"
#include "Util/FlatMap.hpp"

namespace Ember
{
struct OmniLight;

class Camera;
struct RenderCommandQueue;

namespace Internal
{

class OmniLightManager
{
  using BumpAllocator                      = std::pmr::monotonic_buffer_resource;
  using QueryUnallocatedLights             = flecs::query<WorldTransform const, OmniLight const>;
  using QueryAllocatedLights               = flecs::query<WorldTransform const, OmniLight const, OmniLightHandle const>;

  uint16_t constexpr static kMaxOmniLights = 32;
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

  RenderDevice*                     m_RenderDevice{ nullptr };
  World*                            m_World{ nullptr };
  Buffer                            m_ShadowProjectionBuffer;
  OmniLightRepr                     m_LightData[kMaxOmniLights]{};
  uint16_t                          m_IndirectionMap[kMaxOmniLights]{};
  uint16_t                          m_HandleGeneration[kMaxOmniLights]{};
  ComPtr<ID3D12RootSignature>       m_RootSignature;
  ComPtr<ID3D12PipelineState>       m_Pipeline;
  std::vector<Buffer>               m_DataBuffers;
  std::queue<Texture>               m_ShadowCache;
  FlatMap<OmniLightHandle, Texture> m_ShadowsInUse;
  QueryUnallocatedLights            m_InitShadowQuery;
  QueryAllocatedLights              m_DynamicShadowQuery;
  uint16_t                          m_IndirectionFreeHead{ UINT16_MAX };
  uint16_t                          m_TotalLightCount{ 0 };
  uint16_t                          m_ShadowingLightCount{ 0 };
  uint8_t                           m_DirtyFrames{ 0 };

  BumpAllocator                     m_BumpAlloc;

  static_assert( std::numeric_limits<std::remove_cvref_t<decltype( m_IndirectionMap[0] )>>::max() > kMaxOmniLights );

  void         SetDirty();
  void         SwapTrueLocations( uint16_t first, uint16_t second );

  SRVHandle    AllocateOmniShadow( OmniLightHandle omni_light_idx );
  void         FreeOmniShadow( OmniLightHandle omni_light_idx );

  static float CalculateRange( Color32 color, float intensity );

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

  OmniLightHandle AddOmniLight(
      DirectX::XMFLOAT3 position, float range, Color32 color, float intensity, float attenuation = 1.0f );
  OmniLightHandle AddShadowingOmniLight(
      DirectX::XMFLOAT3 position, float range, Color32 color, float intensity, float attenuation = 1.0f );
  void                   Free( OmniLightHandle omni_light_handle );

  SRVHandle              PrepareFrame( uint32_t frame_index );
  [[nodiscard]] uint16_t GetOmniLightCount() const;
  [[nodiscard]] uint16_t GetShadowingOmniLightCount() const;

  //
  void RenderAllShadows(
      ID3D12GraphicsCommandList6* command_list,
      DrawList::Batches const&    draw_list,
      RenderTargetManager const&  rtm,
      Camera const&               camera );

  void RenderOmniShadow(
      ID3D12GraphicsCommandList6* command_list,
      DrawList::Batches const&    draw_list,
      RenderTargetManager const&  rtm,
      OmniLightRepr const&        omni_light,
      Texture const&              texture ) const;

  OmniLightManager( OmniLightManager const& other )                = delete;
  OmniLightManager( OmniLightManager&& other ) noexcept            = delete;
  OmniLightManager& operator=( OmniLightManager const& other )     = delete;
  OmniLightManager& operator=( OmniLightManager&& other ) noexcept = delete;
  ~OmniLightManager();
};
} // namespace Internal

struct OmniLight
{
  float constexpr static kRangeAuto = -1.0f;

  Color32 Color{ Color32::White() };
  float   Range{ kRangeAuto };
  float   Intensity{ 1.0f };
  bool    CastsShadow{ false };
};

} // namespace Ember
