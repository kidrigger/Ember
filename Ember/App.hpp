#pragma once

#include "Util/Runtime.hpp"

namespace Ember
{
class PerfCounter;
class RenderDevice;

class App
{
  HWND          m_WindowHandle{ nullptr };
  RenderDevice* m_RenderDevice{ nullptr };
  PerfCounter*  m_PerfCounter{ nullptr };
  wchar_t       m_SprintfBuffer[1024]{};

  static App*   m_Instance;

public:
  App( HWND window_handle, RenderDevice* render_device, PerfCounter* perf_counter );
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
