#pragma once

#include "Environment.hpp"
#include "IApp.hpp"
#include "RenderDevice.hpp"
#include "RenderTargetManager.hpp"
#include "Scene.hpp"
#include "Util/DirectXHeaders.hpp"
#include "Util/Runtime.hpp"

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

  // PBR Pipeline
  ComPtr<ID3D12RootSignature>          m_RootSignature;
  ComPtr<ID3D12PipelineState>          m_MainPipeline;
  ComPtr<ID3D12PipelineState>          m_BackgroundPipeline;

  std::unique_ptr<RenderTargetManager> m_RenderTargetManager;
  Texture                              m_RenderTexture;
  Texture                              m_DepthTexture;
  DXGI_FORMAT                          m_SwapchainFormat;

  std::unique_ptr<Camera>              m_Camera;
  uint32_t                             m_PrevMouseX{ 0 };
  uint32_t                             m_PrevMouseY{ 0 };
  Buffer                               m_ConfigurationBuffer;

  std::unique_ptr<LightManager>        m_LightManager;

  std::unique_ptr<Environment>         m_Environment;
  std::unique_ptr<MaterialManager>     m_MaterialManager;
  std::unique_ptr<GeometryManager>     m_GeometryManager;
  World                                m_World;
  DrawList                             m_DrawList;
  RenderQueryType                      m_RenderQuery;

  void                                 SetupRenderPipeline();

public:
  BasicApp(
      HWND                                 window_handle,
      std::unique_ptr<RenderDevice>        render_device,
      std::unique_ptr<PerfCounter>         perf_counter,
      std::unique_ptr<RenderTargetManager> render_target_manager );

  void LoadContent() override;
  void Update() override;
  void RenderScene(
      ID3D12GraphicsCommandList6* command_list, DrawList::Info const& draw_list_info, uint32_t frame_idx );
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
