
#include "BasicApp.hpp"
#include "RenderDevice.hpp"
#include "Util/DirectXHeaders.hpp"
#include "Util/HelperUtils.hpp"
#include "Util/Runtime.hpp"

#pragma comment( lib, "dxguid.lib" )

int CALLBACK wWinMain(
    HINSTANCE const                  instance_handle,
    [[maybe_unused]] HINSTANCE const prev_instance_handle,
    [[maybe_unused]] PWSTR const     lp_cmd_line,
    [[maybe_unused]] int const       n_cmd_show )
{
  byte* mem = new byte[sizeof( Ember::BasicApp )];
  auto* app = ( Ember::BasicApp* )mem;
  Ember::BasicApp::Create( app, instance_handle );

  app->LoadContent();

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
      app->Update();
      app->Render();
    }
  }

  app->UnloadContent();

  app->~BasicApp();

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
