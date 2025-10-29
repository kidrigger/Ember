#include "IApp.hpp"

#include <imgui.h>

#include "Input.hpp"
#include "Util/FlatMap.hpp"
#include "Util/HelperUtils.hpp"

Ember::IApp* Ember::IApp::m_Instance = nullptr;

Ember::IApp& Ember::IApp::Instance()
{
  ASSERT( m_Instance );
  return *m_Instance;
}

Ember::IApp::IApp( nullptr_t )
{
  ASSERT_M( not m_Instance, "Only one instance of App allowed at once" );
  m_Instance = this;
}

Ember::IApp::~IApp()
{
  if ( m_Instance == this ) m_Instance = nullptr;
}

// Forward declare message handler from imgui_impl_win32.cpp
// ReSharper disable once CppInconsistentNaming
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler( HWND, UINT, WPARAM, LPARAM );

// Window callback function.
LRESULT CALLBACK WndProc( HWND const window_handle, UINT const message, WPARAM const w_param, LPARAM const l_param )
{
  if ( ImGui_ImplWin32_WndProcHandler( window_handle, message, w_param, l_param ) ) return true;

  switch ( message )
  {
    case WM_PAINT:
      PAINTSTRUCT ps;
      ( void )BeginPaint( window_handle, &ps );
      EndPaint( window_handle, &ps );
      break;
    case WM_SYSKEYDOWN:
    case WM_KEYDOWN:
    {
      // bool alt = (::GetAsyncKeyState(VK_MENU) & 0x8000) != 0;

      if ( w_param == VK_ESCAPE )
      {
        ::PostQuitMessage( 0 );
      }
      else if ( w_param >= 'A' and w_param <= 'Z' )
      {
        Ember::Input::Instance().SetDown( ( char )w_param, true );
      }
    }
    break;
    case WM_KEYUP:
    {
      // bool alt = (::GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
      if ( w_param >= 'A' and w_param <= 'Z' )
      {
        Ember::Input::Instance().SetDown( ( char )w_param, false );
      }
    }
    break;
    case WM_MOUSEMOVE:
    {
      // Handle mouse movement, potentially updating a drawing
      Ember::Input::Instance().SetMousePosition( { LOWORD( l_param ), HIWORD( l_param ) } );
    }
    break;
    case WM_RBUTTONDOWN:
    {
      Ember::Input::Instance().SetRightMouseDown( true );
    }
    break;
    case WM_RBUTTONUP:
    {
      Ember::Input::Instance().SetRightMouseDown( false );
    }
    break;
    case WM_LBUTTONDOWN:
    {
      Ember::Input::Instance().SetLeftMouseDown( true );
    }
    break;
    case WM_LBUTTONUP:
    {
      Ember::Input::Instance().SetLeftMouseDown( false );
    }
    break;
    // The default window procedure will play a system notification sound
    // when pressing the Alt+Enter keyboard combination if this message is
    // not handled.
    case WM_SYSCHAR:
      break;
    case WM_SIZE:
    {
      Ember::IApp::Instance().Resize();
    }
    break;
    case WM_DESTROY:
      ::PostQuitMessage( 0 );
      break;
    default:
      return ::DefWindowProcW( window_handle, message, w_param, l_param );
  }

  return 0;
}

void RegisterWindowClass( HINSTANCE const instance_handle, const wchar_t* window_class_name )
{
  // Register a window class for creating our render window with.
  WNDCLASSEXW window_class;
  window_class.cbSize        = sizeof( WNDCLASSEX );
  window_class.style         = CS_HREDRAW | CS_VREDRAW;
  window_class.lpfnWndProc   = &WndProc;
  window_class.cbClsExtra    = 0;
  window_class.cbWndExtra    = 0;
  window_class.hInstance     = instance_handle;
  window_class.hIcon         = ::LoadIcon( instance_handle, nullptr );
  window_class.hCursor       = ::LoadCursor( nullptr, IDC_ARROW );
  window_class.hbrBackground = ( HBRUSH )( COLOR_WINDOW + 1 );
  window_class.lpszMenuName  = nullptr;
  window_class.lpszClassName = window_class_name;
  window_class.hIconSm       = ::LoadIcon( instance_handle, nullptr );

  static ATOM atom           = ::RegisterClassExW( &window_class );
  ASSERT( atom > 0 );
}

HWND CreateWindow(
    wchar_t const*  window_class_name,
    HINSTANCE const instance_handle,
    wchar_t const*  window_title,
    uint32_t const  width,
    uint32_t const  height )
{
  int const screen_width  = ::GetSystemMetrics( SM_CXSCREEN );
  int const screen_height = ::GetSystemMetrics( SM_CYSCREEN );

  RECT      window_rect   = { 0, 0, ( LONG )width, ( LONG )height };
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
      instance_handle,
      nullptr );

  ASSERT_M( h_window, "Failed to create window" );

  return h_window;
}
