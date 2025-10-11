#pragma once

#include "BackgroundPass.hpp"
#include "Environment.hpp"
#include "ForwardPass.hpp"
#include "FrameGraphHelper.hpp"
#include "GBufferPass.hpp"
#include "IApp.hpp"
#include "LightingPass.hpp"
#include "RenderDevice.hpp"
#include "RenderPassCommon.hpp"
#include "RenderTargetManager.hpp"
#include "Scene.hpp"
#include "TransparencyPass.hpp"
#include "Util/DirectXHeaders.hpp"
#include "Util/Runtime.hpp"
#include "fg/Blackboard.hpp"
#include "fg/FrameGraph.hpp"

namespace Ember
{
class LightManager;
class ModelLoader;
class PerfCounter;
class RenderDevice;
class Camera;

class BasicApp final : public IApp
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

  // Specifics
  FG::Context          m_FGContext;
  FrameGraphBlackboard m_FGBlackboard;

  // PBR Pipeline

  // Forward Only
  RenderPass::OpaqueForward m_OpaquePass;

  // Deferred Only
  RenderPass::GBuffer                  m_GBufferPass;
  RenderPass::OmniLightDeferred        m_OmniLightPass;
  RenderPass::SpotLightDeferred        m_SpotLightPass;
  RenderPass::ScreenSpaceLightDeferred m_ScreenSpaceLightPass;

  // Forward + Deferred
  RenderPass::AlphaTestedForward       m_AlphaTestedPass;
  RenderPass::TransparencyForward      m_TransparencyPass;
  RenderPass::Background               m_BackgroundPass;

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

  void                          SetupRenderPasses();

  RenderPass::RTVData           ClearRenderTargets( FrameGraph* frame_graph ) const;
  RenderPass::RTVData           RenderTransparency( FrameGraph* frame_graph, RenderPass::RTVData const& opaque_pass );
  RenderPass::RTVData           RenderOpaqueFwd( FrameGraph* frame_graph, RenderPass::RTVData const& clear_rtv );
  RenderPass::RTVData           RenderSkybox( FrameGraph* frame_graph, RenderPass::RTVData const& transparency_pass );
  RenderPass::RTVData           RenderOpaqueDfr( FrameGraph* frame_graph, RenderPass::RTVData const& clear_rtv );

public:
  BasicApp(
      HWND                                 window_handle,
      std::unique_ptr<RenderDevice>        render_device,
      std::unique_ptr<PerfCounter>         perf_counter,
      std::unique_ptr<RenderTargetManager> render_target_manager );

  void        LoadContent() override;
  void        Update() override;

  void        Render() override;
  void        UnloadContent() override;

  void        Resize() override;

  static void Create( BasicApp* app, HINSTANCE instance_handle );

  BasicApp( BasicApp const& other )                = delete;
  BasicApp& operator=( BasicApp const& other )     = delete;
  BasicApp( BasicApp&& other ) noexcept            = delete;
  BasicApp& operator=( BasicApp&& other ) noexcept = delete;
  ~BasicApp() override;
};

} // namespace Ember
