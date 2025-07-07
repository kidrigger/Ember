#pragma once

#include "DepthBuffer.hpp"
#include "Util/DirectXHeaders.hpp"
#include "Util/Runtime.hpp"

namespace Ember
{
class PerfCounter;
class RenderDevice;

class App
{
  HWND                          m_WindowHandle{ nullptr };
  uint32_t                      m_WindowWidth{ 1280 };
  uint32_t                      m_WindowHeight{ 720 };

  std::unique_ptr<RenderDevice> m_RenderDevice;
  std::unique_ptr<PerfCounter>  m_PerfCounter;
  wchar_t                       m_SprintfBuffer[1024]{};

  // Specifics
  ComPtr<ID3D12RootSignature> m_RootSignature;
  ComPtr<ID3D12PipelineState> m_PipelineState;

  DepthBuffer                 m_DepthBuffer;

  // Model Specific
  DirectX::XMMATRIX m_GlobalTransform;

  static App*       m_Instance;

public:
  App( HWND window_handle, std::unique_ptr<RenderDevice>&& render_device, std::unique_ptr<PerfCounter>&& perf_counter );
  static App& Instance();

  void        LoadContent();
  void        Update();
  void        Render();
  void        UnloadContent();

  void        Resize();

  static App  Create( HINSTANCE instance_handle );

  App( App const& other )                = delete;
  App& operator=( App const& other )     = delete;
  App( App&& other ) noexcept            = default;
  App& operator=( App&& other ) noexcept = default;
  ~App();
};

} // namespace Ember
