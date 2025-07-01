#include "App.hpp"

#include "Base/HelperUtils.hpp"

#include <cstdint>
#include <utility>

#include "RenderDevice.hpp"

Ember::App* Ember::App::m_Instance{ nullptr };

void        ParseArguments( bool* use_warp, uint32_t* client_width, uint32_t* client_height )
{
  // Parse Args
  {
    int       argc;
    wchar_t** argv = ::CommandLineToArgvW( ::GetCommandLineW(), &argc );

    for ( size_t i = 0; i < argc; ++i )
    {
      if ( ::wcscmp( argv[i], L"-w" ) == 0 or ::wcscmp( argv[i], L"--width" ) == 0 )
      {
        *client_width = ::wcstol( argv[++i], nullptr, 10 );
      }
      if ( ::wcscmp( argv[i], L"-h" ) == 0 or ::wcscmp( argv[i], L"--height" ) == 0 )
      {
        *client_height = ::wcstol( argv[++i], nullptr, 10 );
      }
      if ( ::wcscmp( argv[i], L"-warp" ) == 0 or ::wcscmp( argv[i], L"--warp" ) == 0 )
      {
        *use_warp = true;
      }
    }

    // Free memory allocated by CommandLineToArgvW
    ::LocalFree( argv );
  }
}

// Window callback function.
LRESULT CALLBACK WndProc( HWND const window_handle, UINT const message, WPARAM const w_param, LPARAM const l_param )
{
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
      Ember::App::Instance().Resize();
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
  WNDCLASSEXW window_class   = {};

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
  assert( atom > 0 );
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
      instance_handle,
      nullptr );

  ASSERT_M( h_window, "Failed to create window" );

  return h_window;
}

Ember::App::App( HWND const window_handle, RenderDevice* render_device )
  : m_WindowHandle{ window_handle }, m_RenderDevice{ render_device }
{
  ASSERT_M( not m_Instance, "Second instance being created" );
  m_Instance = this;
}

Ember::App::~App()
{
  ASSERT( not m_RenderDevice );
}

Ember::App& Ember::App::Instance()
{
  return *m_Instance;
}

Ember::App Ember::App::Create( HINSTANCE const instance_handle )
{
  // Windows 10 Creators update adds Per Monitor V2 DPI awareness context.
  // Using this awareness context allows the client area of the window
  // to achieve 100% scaling while still allowing non-client window content to
  // be rendered in a DPI sensitive fashion.
  SetThreadDpiAwarenessContext( DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 );

  // Window class name. Used for registering / creating the window.
  const wchar_t* window_class_name = L"DX12WindowClass";

  // Config
  bool     use_warp      = false;
  uint32_t client_width  = 1280;
  uint32_t client_height = 720;
  ParseArguments( &use_warp, &client_width, &client_height );

  RegisterWindowClass( instance_handle, window_class_name );
  HWND const window_handle =
      CreateWindow( window_class_name, instance_handle, L"Learning DirectX 12", client_width, client_height );

  RenderDevice* render_device = new RenderDevice{ RenderDevice::Create( window_handle, use_warp ) };

  return App{
    window_handle,
    render_device,
  };
}

void Ember::App::LoadContent()
{
  ERR_ABORT( ::ShowWindow( m_WindowHandle, SW_SHOW ) );
}

void Ember::App::Update()
{}

void Ember::App::Render()
{
  ID3D12CommandAllocator*    command_allocator = m_RenderDevice->GetCurrentCommandAllocator();
  ID3D12Resource*            backbuffer        = m_RenderDevice->GetCurrentBackbuffer();
  ID3D12GraphicsCommandList* command_list      = m_RenderDevice->GetGraphicsCommandList();

  ERR_ABORT( command_allocator->Reset() );
  ERR_ABORT( command_list->Reset( command_allocator, nullptr ) );

  // Clear Backbuffer
  CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
      backbuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET );

  command_list->ResourceBarrier( 1, &barrier );

  FLOAT constexpr cornflower_blue[]       = { 0.4f, 0.6f, 0.9f, 1.0f };
  CD3DX12_CPU_DESCRIPTOR_HANDLE const rtv = m_RenderDevice->GetCurrentRTVCpuDescriptorHandle();
  command_list->ClearRenderTargetView( rtv, cornflower_blue, 0, nullptr );

  barrier = CD3DX12_RESOURCE_BARRIER::Transition(
      backbuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT );
  command_list->ResourceBarrier( 1, &barrier );

  ERR_ABORT( command_list->Close() );

  m_RenderDevice->ExecuteCommandList( command_list );

  m_RenderDevice->Present();
}

void Ember::App::UnloadContent()
{}

void Ember::App::Resize() const
{
  RECT rect;
  ::GetWindowRect( m_WindowHandle, &rect );

  UINT const width  = rect.right - rect.left;
  UINT const height = rect.bottom - rect.top;

  m_RenderDevice->ResizeSwapchain( width, height );
}

void Ember::App::Destroy()
{
  m_RenderDevice->Destroy();
  delete m_RenderDevice;
  m_RenderDevice = nullptr;
}
