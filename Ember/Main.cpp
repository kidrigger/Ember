
#include "BasicApp.hpp"
#include "DeferredApp.hpp"
#include "RenderDevice.hpp"
#include "Util/DirectXHeaders.hpp"
#include "Util/HelperUtils.hpp"
#include "Util/Profiling.hpp"
#include "Util/Runtime.hpp"

#pragma comment( lib, "dxguid.lib" )

using AppType = Ember::BasicApp;

int CALLBACK wWinMain(
    HINSTANCE const                  instance_handle,
    [[maybe_unused]] HINSTANCE const prev_instance_handle,
    [[maybe_unused]] PWSTR const     lp_cmd_line,
    [[maybe_unused]] int const       n_cmd_show )
{
  byte* mem = new byte[sizeof( AppType )];
  auto* app = ( AppType* )mem;
  AppType::Create( app, instance_handle );

  app->LoadContent();

  MSG msg = {};
  while ( msg.message != WM_QUIT )
  {
    ZoneScoped;
    if ( ::PeekMessage( &msg, nullptr, 0, 0, PM_REMOVE ) )
    {
      ::TranslateMessage( &msg );
      ::DispatchMessage( &msg );
    }
    else
    {
      app->Update();
      app->Render();
      FrameMark;
    }
  }

  app->UnloadContent();

  app->~AppType();

  delete[] mem;

#if defined( _DEBUG )
  ComPtr<IDXGIDebug1> debug_device;
  if ( SUCCEEDED( DXGIGetDebugInterface1( 0, IID_PPV_ARGS( &debug_device ) ) ) )
  {
    ERR_ABORT( debug_device->ReportLiveObjects(
        DXGI_DEBUG_ALL, ( DXGI_DEBUG_RLO_FLAGS )( DXGI_DEBUG_RLO_IGNORE_INTERNAL | DXGI_DEBUG_RLO_SUMMARY ) ) );
  }
#endif

  return 0;
}
