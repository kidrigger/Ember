#pragma once

#include "BackgroundPass.hpp"
#include "Environment.hpp"
#include "FrameGraphHelper.hpp"
#include "GBufferPass.hpp"
#include "IApp.hpp"
#include "LightingPass.hpp"
#include "RenderDevice.hpp"
#include "RenderTargetManager.hpp"
#include "Scene.hpp"
#include "TransparencyPass.hpp"
#include "Util/DirectXHeaders.hpp"
#include "fg/FrameGraph.hpp"
#include "fg/FrameGraphResource.hpp"

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

  HWND                                 m_WindowHandle{ nullptr };
  uint32_t                             m_WindowWidth{ 1280 };
  uint32_t                             m_WindowHeight{ 720 };

  std::unique_ptr<RenderDevice>        m_RenderDevice;
  std::unique_ptr<PerfCounter>         m_PerfCounter;
  std::unique_ptr<TextureLoader>       m_TextureLoader;
  std::unique_ptr<ModelLoader>         m_ModelLoader;
  wchar_t                              m_SprintfBuffer[1024]{};

  RenderPass::GBuffer                  m_GBufferPass;
  RenderPass::TransparencyForward      m_TransparencyPass;
  RenderPass::OmniLightDeferred        m_OmniLightPass;
  RenderPass::SpotLightDeferred        m_SpotLightPass;
  RenderPass::ScreenSpaceLightDeferred m_ScreenSpaceLightPass;
  RenderPass::Background               m_BackgroundPass;
  FG::Context                          m_FGContext;

  std::unique_ptr<RenderTargetManager> m_RenderTargetManager;
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

  void                LoadContent() override;
  void                Update() override;
  RenderPass::RTVData RenderScene(
      ID3D12GraphicsCommandList6* command_list,
      DrawList::Batches const&    draw_list_info_list,
      FrameGraph*                 frame_graph,
      uint32_t                    frame_idx );
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
