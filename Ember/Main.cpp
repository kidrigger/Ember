
#include "App.hpp"
#include "Base/DirectXHeaders.hpp"
#include "Base/HelperUtils.hpp"
#include "Base/Runtime.hpp"
#include "RenderDevice.hpp"

int CALLBACK wWinMain(
    HINSTANCE const                  instance_handle,
    [[maybe_unused]] HINSTANCE const prev_instance_handle,
    [[maybe_unused]] PWSTR const     lp_cmd_line,
    [[maybe_unused]] int const       n_cmd_show )
{
  Ember::App app = Ember::App::Create( instance_handle );

  app.LoadContent();

  MSG msg = {};
  while ( msg.message != WM_QUIT )
  {
    if ( ::PeekMessage( &msg, nullptr, 0, 0, PM_REMOVE ) )
    {
      ::TranslateMessage( &msg );
      ::DispatchMessage( &msg );
    }
    else
    {
      app.Update();
      app.Render();
    }
  }

  app.UnloadContent();
  app.Destroy();

  return 0;
}
