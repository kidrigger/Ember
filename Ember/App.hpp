#pragma once

#include "Base/Runtime.hpp"

namespace Ember
{
class RenderDevice;

class App
{
  HWND          m_WindowHandle{ nullptr };
  RenderDevice* m_RenderDevice{ nullptr };
  LARGE_INTEGER m_PrevQueryPerfCounter{};
  double        m_DeltaTimeSeconds{ 0.0f };
  double        m_DeltaTimeMilliseconds{ 0.0f };
  wchar_t       m_SprintfBuffer[1024]{};
  double        m_256FrameAvgBuffer[256]{};
  double        m_BufferSumMs{ 0.0f };
  int           m_AvgBufferHead{ 0 };

  static App*   m_Instance;

public:
  App( HWND window_handle, RenderDevice* render_device );
  ~App();

  static App& Instance();

  void        LoadContent();
  void        Update();
  void        Render();
  void        UnloadContent();

  void        Resize() const;

  static App  Create( HINSTANCE instance_handle );
  void        Destroy();
};

} // namespace Ember
