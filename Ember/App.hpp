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
  HWND          m_WindowHandle{ nullptr };
  uint32_t      m_WindowWidth{ 1280 };
  uint32_t      m_WindowHeight{ 720 };

  RenderDevice* m_RenderDevice{ nullptr };
  PerfCounter*  m_PerfCounter{ nullptr };
  wchar_t       m_SprintfBuffer[1024]{};

  // Specifics
  ComPtr<ID3D12RootSignature> m_RootSignature;
  ComPtr<ID3D12PipelineState> m_PipelineState;

  DepthBuffer                 m_DepthBuffer;

  // Model Specific
  DirectX::XMMATRIX m_GlobalTransform;

  static App*       m_Instance;

public:
  App( HWND window_handle, RenderDevice* render_device, PerfCounter* perf_counter );

  static App& Instance();

  void        LoadContent();
  void        Update();
  void        Render();
  void        UnloadContent();

  void        Resize();

  static App  Create( HINSTANCE instance_handle );
  void        Destroy();
};

} // namespace Ember
