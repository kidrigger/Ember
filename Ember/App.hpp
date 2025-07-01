#pragma once

#include "Base/Runtime.hpp"

namespace Ember
{
class RenderDevice;

class App
{
  HWND          m_WindowHandle{ nullptr };
  RenderDevice* m_RenderDevice{ nullptr };
  static App*   m_Instance;

public:
  App( HWND window_handle, RenderDevice* render_device );
  ~App();

  static App& Instance();

  static App  Create( HINSTANCE instance_handle );

  void        LoadContent();
  void        Update();
  void        Render();
  void        UnloadContent();

  void        Destroy();
};

} // namespace Ember
