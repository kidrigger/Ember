
#include "Base/DirectXHeaders.hpp"
#include "Base/HelperUtils.hpp"
#include "Base/Runtime.hpp"
#include "RenderDevice.hpp"

// Window callback function.
LRESULT CALLBACK WndProc( HWND, UINT, WPARAM, LPARAM );

void             RegisterWindowClass( HINSTANCE h_instance, const wchar_t* window_class_name )
{
  // Register a window class for creating our render window with.
  WNDCLASSEXW window_class   = {};

  window_class.cbSize        = sizeof( WNDCLASSEX );
  window_class.style         = CS_HREDRAW | CS_VREDRAW;
  window_class.lpfnWndProc   = &WndProc;
  window_class.cbClsExtra    = 0;
  window_class.cbWndExtra    = 0;
  window_class.hInstance     = h_instance;
  window_class.hIcon         = ::LoadIcon( h_instance, nullptr );
  window_class.hCursor       = ::LoadCursor( nullptr, IDC_ARROW );
  window_class.hbrBackground = ( HBRUSH )( COLOR_WINDOW + 1 );
  window_class.lpszMenuName  = nullptr;
  window_class.lpszClassName = window_class_name;
  window_class.hIconSm       = ::LoadIcon( h_instance, nullptr );

  static ATOM atom           = ::RegisterClassExW( &window_class );
  assert( atom > 0 );
}

HWND CreateWindow(
    wchar_t const*  window_class_name,
    HINSTANCE const h_instance,
    wchar_t const*  window_title,
    uint32_t const  width,
    uint32_t const  height )
{
  int const screen_width  = ::GetSystemMetrics( SM_CXSCREEN );
  int const screen_height = ::GetSystemMetrics( SM_CYSCREEN );

  RECT      window_rect   = { 0, 0, static_cast<LONG>( width ), static_cast<LONG>( height ) };
  ::AdjustWindowRect( &window_rect, WS_OVERLAPPEDWINDOW, FALSE );

  int const window_width  = window_rect.right - window_rect.left;
  int const window_height = window_rect.bottom - window_rect.top;

  // Center the window within the screen. Clamp to 0, 0 for the top-left corner.
  int const  window_x = std::max<int>( 0, ( screen_width - window_width ) / 2 );
  int const  window_y = std::max<int>( 0, ( screen_height - window_height ) / 2 );

  HWND const h_window = ::CreateWindowExW(
      NULL,
      window_class_name,
      window_title,
      WS_OVERLAPPEDWINDOW,
      window_x,
      window_y,
      window_width,
      window_height,
      nullptr,
      nullptr,
      h_instance,
      nullptr );

  ASSERT_M( h_window, "Failed to create window" );

  return h_window;
}

LRESULT CALLBACK WndProc( HWND window, UINT message, WPARAM w_param, LPARAM l_param )
{
  switch ( message )
  {
    case WM_PAINT:
      break;
    case WM_SYSKEYDOWN:
    case WM_KEYDOWN:
    {
      // bool alt = (::GetAsyncKeyState(VK_MENU) & 0x8000) != 0;

      switch ( w_param )
      {
          /*case 'V':
            g_VSync = !g_VSync;
            break;*/
        case VK_ESCAPE:
          ::PostQuitMessage( 0 );
          break;
          /*case VK_RETURN:
            if (alt)
            {
          case VK_F11:
            SetFullscreen(!g_Fullscreen);
            }
            break;*/
      }
    }
    break;
    // The default window procedure will play a system notification sound
    // when pressing the Alt+Enter keyboard combination if this message is
    // not handled.
    case WM_SYSCHAR:
      break;
    case WM_SIZE:
    {
    }
    break;
    case WM_DESTROY:
      ::PostQuitMessage( 0 );
      break;
    default:
      return ::DefWindowProcW( window, message, w_param, l_param );
  }

  return 0;
}

int CALLBACK wWinMain( HINSTANCE h_instance, HINSTANCE h_prev_instance, PWSTR lp_cmd_line, int n_cmd_show )
{
  // Config
  bool     use_warp      = false;
  uint32_t client_width  = 1280;
  uint32_t client_height = 720;

  // Windows 10 Creators update adds Per Monitor V2 DPI awareness context.
  // Using this awareness context allows the client area of the window
  // to achieve 100% scaling while still allowing non-client window content to
  // be rendered in a DPI sensitive fashion.
  SetThreadDpiAwarenessContext( DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 );

  // Window class name. Used for registering / creating the window.
  const wchar_t* window_class_name = L"DX12WindowClass";

  // Parse Args
  {
    int       argc;
    wchar_t** argv = ::CommandLineToArgvW( ::GetCommandLineW(), &argc );

    for ( size_t i = 0; i < argc; ++i )
    {
      if ( ::wcscmp( argv[i], L"-w" ) == 0 or ::wcscmp( argv[i], L"--width" ) == 0 )
      {
        client_width = ::wcstol( argv[++i], nullptr, 10 );
      }
      if ( ::wcscmp( argv[i], L"-h" ) == 0 or ::wcscmp( argv[i], L"--height" ) == 0 )
      {
        client_height = ::wcstol( argv[++i], nullptr, 10 );
      }
      if ( ::wcscmp( argv[i], L"-warp" ) == 0 or ::wcscmp( argv[i], L"--warp" ) == 0 )
      {
        use_warp = true;
      }
    }

    // Free memory allocated by CommandLineToArgvW
    ::LocalFree( argv );
  }

  RegisterWindowClass( h_instance, window_class_name );
  HWND const window_handle =
      CreateWindow( window_class_name, h_instance, L"Learning DirectX 12", client_width, client_height );

  Ember::RenderDevice app = Ember::RenderDevice::Create( window_handle, use_warp );

  ERR_ABORT( ::ShowWindow( window_handle, SW_SHOW ) );

  MSG msg = {};
  while ( msg.message != WM_QUIT )
  {
    if ( ::PeekMessage( &msg, nullptr, 0, 0, PM_REMOVE ) )
    {
      ::TranslateMessage( &msg );
      ::DispatchMessage( &msg );
    }
  }

  app.Destroy();

  return 0;
}
