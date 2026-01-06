#pragma once

#include <Graphics/RenderDevice.hpp>
#include <Util/DirectXHeaders.hpp>
#include <Util/Runtime.hpp>
#include "Atmosphere.hpp"
#include "DepthPrePass.hpp"
#include "Environment.hpp"
#include "ForwardPass.hpp"
#include "FrameGraphHelper.hpp"
#include "GBufferPass.hpp"
#include "IApp.hpp"
#include "LightingPass.hpp"
#include "RenderPassCommon.hpp"
#include "Scene.hpp"
#include "SkyboxPass.hpp"
#include "TransparencyPass.hpp"
#include "fg/Blackboard.hpp"

namespace Ember
{
class LightManager;
class ModelLoader;
class PerfCounter;
class RenderDevice;
class TextureLoader;
class Camera;

class BasicApp final : public IApp
{
  using RenderQueryType = flecs::query<WorldTransform const, Mesh const, Geometry const, Material const>;

  DXGI_FORMAT constexpr static kDepthFormat = DXGI_FORMAT_D32_FLOAT;

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
  RenderPass::DepthPrePass m_DrawPrePass;

  // Forward Only
  RenderPass::OpaqueForward m_RenderOpaqueMeshes;

  // Deferred Only
  RenderPass::GBuffer                  m_UpdateGBuffer;
  RenderPass::OmniLightDeferred        m_RenderOmniLights;
  RenderPass::SpotLightDeferred        m_RenderSpotLights;
  RenderPass::ScreenSpaceLightDeferred m_RenderScreenSpaceLighting;

  // Forward + Deferred
  RenderPass::AlphaTestedForward  m_RenderAlphaTestedMeshes;
  RenderPass::TransparencyForward m_RenderTransparentMeshes;

  // Environment
  RenderPass::Atmosphere           m_UpdateAtmosphericSky;
  RenderPass::Skybox               m_RenderBackground;

  std::unique_ptr<Camera>          m_Camera;
  DirectX::XMUINT2                 m_PrevMouse{};
  Buffer                           m_ConfigurationBuffer;

  std::unique_ptr<Environment>     m_Environment;
  std::unique_ptr<MaterialManager> m_MaterialManager;
  std::unique_ptr<GeometryManager> m_GeometryManager;
  std::unique_ptr<World>           m_World;
  DrawList                         m_DrawList;
  RenderQueryType                  m_RenderQuery;
  // TODO: Organize init and destroy.
  std::unique_ptr<LightManager> m_LightManager;

  flecs::entity                 m_SceneRoot;

  void                          SetupRenderPasses();

  static void                   InitImGui( HWND const window_handle, RenderDevice* render_device );

public:
  BasicApp(
      HWND                             window_handle,
      std::unique_ptr<RenderDevice>    render_device,
      std::unique_ptr<PerfCounter>     perf_counter,
      std::unique_ptr<Camera>          camera,
      std::unique_ptr<Environment>     environment,
      std::unique_ptr<MaterialManager> material_manager,
      std::unique_ptr<GeometryManager> geometry_manager,
      std::unique_ptr<World>           world,
      std::unique_ptr<LightManager>    light_manager,
      std::unique_ptr<TextureLoader>   texture_loader,
      std::unique_ptr<ModelLoader>     model_loader );

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
