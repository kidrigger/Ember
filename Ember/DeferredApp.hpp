#pragma once

#include "Environment.hpp"
#include "IApp.hpp"
#include "RenderDevice.hpp"
#include "RenderTargetManager.hpp"
#include "Scene.hpp"
#include "Util/DataUtil.hpp"
#include "Util/DirectXHeaders.hpp"
#include "Util/FlatMap.hpp"
#include "Util/Runtime.hpp"

namespace Ember
{
class LightManager;
class ModelLoader;
class PerfCounter;
class RenderDevice;
class Camera;

class DeferredApp final : public IApp
{
  using RenderQueryType = flecs::query<WorldTransform const, Mesh const, Geometry const, Material const>;

  HWND                           m_WindowHandle{ nullptr };
  uint32_t                       m_WindowWidth{ 1280 };
  uint32_t                       m_WindowHeight{ 720 };

  std::unique_ptr<RenderDevice>  m_RenderDevice;
  std::unique_ptr<PerfCounter>   m_PerfCounter;
  std::unique_ptr<TextureLoader> m_TextureLoader;
  std::unique_ptr<ModelLoader>   m_ModelLoader;
  wchar_t                        m_SprintfBuffer[1024]{};

  enum GBufferIndex
  {
    kPosition     = 0,
    kAlbedo       = 1,
    kNormal       = 2,
    kORM          = 3,
    kEmissive     = 4,
    kGBufferCount = 5,
  };

  // TODO: Use more compact formats if possible.
  // R32G32B32A32_FLOAT is overkill for position, but required for the shadow.
  // Use quantization?
  // Normal to Octahedron?
  DXGI_FORMAT constexpr static kGBufferFormats[kGBufferCount] = {
    DXGI_FORMAT_R32G32B32A32_FLOAT,  // Position
    DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, // Albedo
    DXGI_FORMAT_R8G8B8A8_UNORM,      // Normal
    DXGI_FORMAT_R8G8B8A8_UNORM,      // ORM
    DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, // Emissive
  };

  // Specifics

  // PBR Pipeline
  ComPtr<ID3D12RootSignature>          m_RootSignature;
  ComPtr<ID3D12PipelineState>          m_GBufferPipeline;
  ComPtr<ID3D12PipelineState>          m_AlphaTestedGBufferPipeline;

  ComPtr<ID3D12RootSignature>          m_MergeRootSignature;
  ComPtr<ID3D12PipelineState>          m_MergePipeline;
  ComPtr<ID3D12PipelineState>          m_OmniLightVolumePipeline;
  ComPtr<ID3D12PipelineState>          m_SpotLightVolumePipeline;
  ComPtr<ID3D12PipelineState>          m_AlphaBlendedPBRPipeline;

  ComPtr<ID3D12RootSignature>          m_BackgroundRootSignature;
  ComPtr<ID3D12PipelineState>          m_BackgroundPipeline;

  std::unique_ptr<RenderTargetManager> m_RenderTargetManager;
  Texture                              m_GBuffer[kGBufferCount];
  Texture                              m_RenderTexture;
  Texture                              m_DepthTexture;
  DXGI_FORMAT                          m_SwapchainFormat;

  std::unique_ptr<Camera>              m_Camera;
  DirectX::XMUINT2                     m_PrevMouse{};
  Buffer                               m_ConfigurationBuffer;

  std::unique_ptr<Environment>         m_Environment;
  std::unique_ptr<MaterialManager>     m_MaterialManager;
  std::unique_ptr<GeometryManager>     m_GeometryManager;
  World                                m_World;
  DrawList                             m_DrawList;
  RenderQueryType                      m_RenderQuery;
  // TODO: Organize init and destroy.
  std::unique_ptr<LightManager> m_LightManager;

  void                          SetupRenderPipeline();

public:
  DeferredApp(
      HWND                                 window_handle,
      std::unique_ptr<RenderDevice>        render_device,
      std::unique_ptr<PerfCounter>         perf_counter,
      std::unique_ptr<RenderTargetManager> render_target_manager );

  void LoadContent() override;
  void Update() override;
  void RenderScene(
      ID3D12GraphicsCommandList6* command_list, DrawList::Batches const& draw_list_info, uint32_t frame_idx );
  void        Render() override;
  void        UnloadContent() override;

  void        Resize() override;

  static void Create( DeferredApp* app, HINSTANCE instance_handle );

  DeferredApp( DeferredApp const& other )                = delete;
  DeferredApp& operator=( DeferredApp const& other )     = delete;
  DeferredApp( DeferredApp&& other ) noexcept            = delete;
  DeferredApp& operator=( DeferredApp&& other ) noexcept = delete;
  ~DeferredApp() override;
};

} // namespace Ember
