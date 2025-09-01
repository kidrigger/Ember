#include "BasicApp.hpp"

#include <cstdint>
#include <unordered_set>
#include <utility>

#include "Camera.hpp"
#include "Environment.hpp"
#include "GeometryManager.hpp"
#include "LightManager.hpp"
#include "Material.hpp"
#include "MaterialManager.hpp"
#include "ModelLoader.hpp"
#include "RenderDevice.hpp"
#include "TextureLoader.hpp"
#include "Util/DataUtil.hpp"
#include "Util/HelperUtils.hpp"
#include "Util/PerfCounter.hpp"
#include "Util/Profiling.hpp"

void ParseArguments( bool* use_warp, uint32_t* client_width, uint32_t* client_height )
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

namespace
{
struct Input
{
  uint32_t MousePosX;
  uint32_t MousePosY;
  bool     IsRightMouseDown;

  uint32_t constexpr static kPressedBit     = 0x1;
  uint32_t constexpr static kPrevPressedBit = 0x2;
  std::unordered_map<char, uint8_t> KeyPress;

  bool                              IsPressed( char c )
  {
    return KeyPress[c] & kPressedBit;
  }

  bool IsJustPressed( char c )
  {
    uint8_t val = KeyPress[c];
    return val & kPressedBit and not( val & kPrevPressedBit );
  }

  bool IsJustReleased( char c )
  {
    uint8_t val = KeyPress[c];
    return val & kPrevPressedBit and not( val & kPressedBit );
  }

  void Update()
  {
    for ( auto& v : KeyPress | std::views::values )
    {
      v = ( v << 1 ) | v;
    }
  }
} g_Input;

} // namespace

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

      if ( w_param == VK_ESCAPE )
      {
        ::PostQuitMessage( 0 );
      }
      else if ( w_param >= 'A' and w_param <= 'Z' )
      {
        g_Input.KeyPress[( char )w_param] |= Input::kPressedBit;
      }
    }
    break;
    case WM_KEYUP:
    {
      // bool alt = (::GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
      if ( w_param >= 'A' and w_param <= 'Z' )
      {
        g_Input.KeyPress[( char )w_param] &= ~Input::kPressedBit;
      }
    }
    break;
    case WM_MOUSEMOVE:
    {
      g_Input.MousePosX = LOWORD( l_param );
      g_Input.MousePosY = HIWORD( l_param );

      // Handle mouse movement, potentially updating a drawing
    }
    break;
    case WM_RBUTTONDOWN:
    {
      g_Input.IsRightMouseDown = true;
    }
    break;
    case WM_RBUTTONUP:
    {
      g_Input.IsRightMouseDown = false;
    }
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

struct RotatingModel
{
  float Speed;
};

struct PerFrameConstants
{
  Ember::SRVHandle MaterialsBuffer;
  Ember::CBVHandle Camera;
  Ember::SRVHandle OmniLightBuffer;
  uint32_t         OmniLightCount;
  uint32_t         OmniLightShadowCount;
  Ember::SRVHandle DirLightBuffer;
  uint32_t         DirLightCount;
  uint32_t         DirLightShadowCount;
};

struct PerMeshConstants
{
  Ember::MaterialHandle MaterialIdx;
  Ember::SRVHandle      VertexBuffer;
  uint32_t              FirstVertex;
  Ember::SRVHandle      MeshletBuffer;
  Ember::SRVHandle      MeshletTriangles;
  Ember::SRVHandle      MeshletVertices;
  uint32_t              FirstMeshlet;
  uint32_t              FirstIndex;
};

Ember::BasicApp::BasicApp(
    HWND                                 window_handle,
    std::unique_ptr<RenderDevice>        render_device,
    std::unique_ptr<PerfCounter>         perf_counter,
    std::unique_ptr<RenderTargetManager> render_target_manager )
  : IApp{ nullptr }
  , m_WindowHandle{ window_handle }
  , m_RenderDevice{ std::move( render_device ) }
  , m_PerfCounter{ std::move( perf_counter ) }
  , m_RenderTargetManager{ std::move( render_target_manager ) }
  , m_SwapchainFormat{ m_RenderDevice->FetchSwapchainFormat() }
  , m_Camera{ std::make_unique<Camera>() }
  , m_LightManager{ std::make_unique_for_overwrite<LightManager>() }
  , m_Environment{ std::make_unique<Environment>() }
  , m_MaterialManager{ std::make_unique_for_overwrite<MaterialManager>() }
  , m_GeometryManager{ std::make_unique_for_overwrite<GeometryManager>() }
  , m_DrawList{ m_RenderDevice.get(), m_GeometryManager.get(), RenderDevice::kNumFrames }
{
  m_TextureLoader = std::make_unique_for_overwrite<TextureLoader>();
  TextureLoader::Create( m_TextureLoader.get(), m_RenderDevice.get(), 3 );
}

void Ember::BasicApp::Create( BasicApp* app, HINSTANCE const instance_handle )
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

  auto render_device = std::make_unique_for_overwrite<RenderDevice>();
  RenderDevice::Create( render_device.get(), window_handle, use_warp );

  auto perf_counter = std::make_unique_for_overwrite<PerfCounter>();
  PerfCounter::Create( perf_counter.get(), render_device->GetDevice().Get(), RenderDevice::kNumFrames );

  auto render_target_manager = std::make_unique_for_overwrite<RenderTargetManager>();
  RenderTargetManager::Create( render_target_manager.get(), render_device.get() );

  new ( app ) BasicApp{
    window_handle,
    std::move( render_device ),
    std::move( perf_counter ),
    std::move( render_target_manager ),
  };
}

Ember::BasicApp::~BasicApp() // NOLINT(modernize-use-equals-default)
{
  m_RenderDevice->WaitIdle();
}

void Ember::BasicApp::SetupRenderPipeline()
{
  ComPtr<ID3DBlob> amp_shader_blob;
  ERR_ABORT( D3DReadFileToBlob( L"TriangleAS.cso", &amp_shader_blob ) );
  ComPtr<ID3DBlob> mesh_shader_blob;
  ERR_ABORT( D3DReadFileToBlob( L"TriangleMS.cso", &mesh_shader_blob ) );
  ComPtr<ID3DBlob> pixel_shader_blob;
  ERR_ABORT( D3DReadFileToBlob( L"TrianglePS.cso", &pixel_shader_blob ) );
  ComPtr<ID3DBlob> bg_mesh_shader_blob;
  ERR_ABORT( D3DReadFileToBlob( L"BackgroundMS.cso", &bg_mesh_shader_blob ) );
  ComPtr<ID3DBlob> bg_pixel_shader_blob;
  ERR_ABORT( D3DReadFileToBlob( L"BackgroundPS.cso", &bg_pixel_shader_blob ) );

  ComPtr<ID3D12Device2>       device                 = m_RenderDevice->GetDevice();

  D3D_ROOT_SIGNATURE_VERSION  root_signature_version = m_RenderDevice->FetchHighestRootSignatureVersion();

  CD3DX12_STATIC_SAMPLER_DESC static_sampler_desc[]  = {
    CD3DX12_STATIC_SAMPLER_DESC{ 0 },
    CD3DX12_STATIC_SAMPLER_DESC{ 1,
                                D3D12_FILTER_ANISOTROPIC, D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
                                D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP },
    CD3DX12_STATIC_SAMPLER_DESC{ 2,
                                D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_BORDER,
                                D3D12_TEXTURE_ADDRESS_MODE_BORDER, D3D12_TEXTURE_ADDRESS_MODE_BORDER,
                                0, 16,
                                D3D12_COMPARISON_FUNC_LESS_EQUAL, D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE },
  };

  D3D12_ROOT_SIGNATURE_FLAGS const root_signature_flags = D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED |
                                                          D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED |
                                                          D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS |
                                                          D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
                                                          D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
                                                          D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS;

  CD3DX12_ROOT_PARAMETER1 root_parameters[3];
  root_parameters[0].InitAsConstants( sizeof( DrawList::Info ) / 4, 0 );
  root_parameters[1].InitAsConstants( sizeof( PerFrameConstants ) / 4, 1 );
  root_parameters[2].InitAsConstants( sizeof( Environment::GpuRepr ) / 4, 2 );

  CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC root_signature_desc;
  root_signature_desc.Init_1_1(
      CountOf( root_parameters ),
      DataOf( root_parameters ),
      CountOf( static_sampler_desc ),
      DataOf( static_sampler_desc ),
      root_signature_flags );

  ComPtr<ID3DBlob> root_signature_blob;
  ComPtr<ID3DBlob> error_blob;
  ERR_ABORT( D3DX12SerializeVersionedRootSignature(
      &root_signature_desc, root_signature_version, &root_signature_blob, &error_blob ) );

  ERR_ABORT( device->CreateRootSignature(
      0,
      root_signature_blob->GetBufferPointer(),
      root_signature_blob->GetBufferSize(),
      IID_PPV_ARGS( &m_RootSignature ) ) );

  D3D12_RT_FORMAT_ARRAY rtv_formats{
    .RTFormats        = { DXGI_FORMAT_R8G8B8A8_UNORM },
    .NumRenderTargets = 1,
  };

  CD3DX12_RASTERIZER_DESC2 rasterizer_desc{ D3D12_DEFAULT };
  rasterizer_desc.FrontCounterClockwise = true;
  rasterizer_desc.CullMode              = D3D12_CULL_MODE_BACK;

  CD3DX12_DEPTH_STENCIL_DESC depth_stencil_desc{ D3D12_DEFAULT };
  depth_stencil_desc.DepthEnable = TRUE;
  depth_stencil_desc.DepthFunc   = D3D12_COMPARISON_FUNC_LESS;

  struct MainPipelineStream
  {
    CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE        RootSignature;
    CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY    PrimitiveTopologyType;
    CD3DX12_PIPELINE_STATE_STREAM_AS                    AS;
    CD3DX12_PIPELINE_STATE_STREAM_MS                    MS;
    CD3DX12_PIPELINE_STATE_STREAM_PS                    PS;
    CD3DX12_PIPELINE_STATE_STREAM_RASTERIZER2           Rasterizer;
    CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS RTVFormats;
    CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL_FORMAT  DSVFormat;
  };

  MainPipelineStream pipeline_stream = {
    .RootSignature         = m_RootSignature.Get(),
    .PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
    .AS                    = CD3DX12_SHADER_BYTECODE( amp_shader_blob.Get() ),
    .MS                    = CD3DX12_SHADER_BYTECODE( mesh_shader_blob.Get() ),
    .PS                    = CD3DX12_SHADER_BYTECODE( pixel_shader_blob.Get() ),
    .Rasterizer            = rasterizer_desc,
    .RTVFormats            = rtv_formats,
    .DSVFormat             = DXGI_FORMAT_D32_FLOAT,
  };

  struct BackgroundPipelineStream
  {
    CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE        RootSignature;
    CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY    PrimitiveTopologyType;
    CD3DX12_PIPELINE_STATE_STREAM_MS                    MS;
    CD3DX12_PIPELINE_STATE_STREAM_PS                    PS;
    CD3DX12_PIPELINE_STATE_STREAM_RASTERIZER2           Rasterizer;
    CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL         DepthStencil;
    CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS RTVFormats;
    CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL_FORMAT  DSVFormat;
  };

  depth_stencil_desc.DepthFunc                = D3D12_COMPARISON_FUNC_LESS_EQUAL;
  BackgroundPipelineStream bg_pipeline_stream = {
    .RootSignature         = m_RootSignature.Get(),
    .PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
    .MS                    = CD3DX12_SHADER_BYTECODE( bg_mesh_shader_blob.Get() ),
    .PS                    = CD3DX12_SHADER_BYTECODE( bg_pixel_shader_blob.Get() ),
    .Rasterizer            = rasterizer_desc,
    .DepthStencil          = depth_stencil_desc,
    .RTVFormats            = rtv_formats,
    .DSVFormat             = DXGI_FORMAT_D32_FLOAT,
  };

  D3D12_PIPELINE_STATE_STREAM_DESC const pipeline_state_stream_desc = {
    .SizeInBytes                   = sizeof pipeline_stream,
    .pPipelineStateSubobjectStream = &pipeline_stream,
  };

  ERR_ABORT( device->CreatePipelineState( &pipeline_state_stream_desc, IID_PPV_ARGS( &m_MainPipeline ) ) );

  D3D12_PIPELINE_STATE_STREAM_DESC const bg_pipeline_state_stream_desc = {
    .SizeInBytes                   = sizeof bg_pipeline_stream,
    .pPipelineStateSubobjectStream = &bg_pipeline_stream,
  };
  ERR_ABORT( device->CreatePipelineState( &bg_pipeline_state_stream_desc, IID_PPV_ARGS( &m_BackgroundPipeline ) ) );
}

void Ember::BasicApp::LoadContent()
{
  ERR_ABORT( ::ShowWindow( m_WindowHandle, SW_SHOW ) );

  // Setup Camera
  Camera::Create( m_Camera.get(), m_RenderDevice.get(), RenderDevice::kNumFrames );

  m_Camera->SetHorizontalFoV( DirectX::XMConvertToRadians( 70.0f ) );
  m_Camera->SetAspectRatio( ( float )m_WindowWidth / ( float )m_WindowHeight );
  m_Camera->SetYawPitch( DirectX::XM_PI * 5.0f / 4.0f, 0.0f );
  m_Camera->SetPosition( DirectX::XMVectorSet( -23.0f, 2.0f, -10.0f, 1.0f ) );

  // Setup Lights
  LightManager::Create( m_LightManager.get(), m_RenderDevice.get(), RenderDevice::kNumFrames );
  m_LightManager->AddShadowingOmniLight( { 15.0f, 2.0f, 12.0f }, 10.0f, Color32::Blue(), 15.0f );
  m_LightManager->AddShadowingOmniLight( { 0.0f, 2.0f, 5.0f }, 10.0f, Color32::Green(), 15.0f );
  m_LightManager->AddShadowingOmniLight( { -15.0f, 2.0f, -5.0f }, 10.0f, Color32::Red(), 25.0f );
  m_LightManager->AddShadowingDirLight( { 1.0f, -1.0f, 0.0f }, Color32::White(), 12.0f );

  MaterialManager::Create( m_MaterialManager.get(), m_RenderDevice.get(), 10'000 );
  GeometryManager::Create( m_GeometryManager.get(), m_RenderDevice.get(), 256_MiB );

  m_ModelLoader = std::make_unique<ModelLoader>(
      m_RenderDevice.get(), &m_World, m_TextureLoader.get(), m_MaterialManager.get(), m_GeometryManager.get() );

  // Setup Scene Geometry
  flecs::entity       model = m_ModelLoader->TryLoadModel( "Bistro.glb" ).value().set_name( "Scene" );

  flecs::entity const rm =
      m_World.GetECS()
          .entity( "HelmetRotator" )
          .insert(
              []( LocalTransform& lt, WorldTransform&, RotatingModel& rm, WorldBoundingBox&, CullInfo& )
              {
                rm.Speed       = 20.0f;
                lt.Translation = DirectX::XMVectorSet( 0.0f, 1.0f, 5.0f, 1.0f );
              } );

  model                    = m_ModelLoader->TryLoadModel( "DamagedHelmet.glb" )->child_of( rm );
  LocalTransform* local_tx = model.get_mut<LocalTransform>();
  local_tx->Scale          = DirectX::XMVectorSet( 0.3f, 0.3f, 0.3f, 0.0f );
  local_tx->Rotation       = DirectX::XMQuaternionIdentity();

  model                    = m_ModelLoader->TryLoadModel( "NormalTangentTest.glb" ).value();
  local_tx                 = model.get_mut<LocalTransform>();
  local_tx->Translation    = DirectX::XMVectorSet( 3.0f, 2.0f, 7.0f, 1.0f );

  model                    = m_ModelLoader->TryLoadModel( "NormalTangentMirrorTest.glb" ).value();
  local_tx                 = model.get_mut<LocalTransform>();
  local_tx->Translation    = DirectX::XMVectorSet( 6.0f, 2.0f, 7.0f, 1.0f );

  // constexpr char const* kEnvMapFile = "OvercastSoil.hdr";
  // bool const            env_loaded =
  //     Environment::TryLoadFrom( m_Environment.get(), m_RenderDevice.get(), m_TextureLoader.get(), kEnvMapFile );
  // ASSERT( env_loaded );

  SetupRenderPipeline();

  m_RenderTexture = m_RenderDevice->CreateTexture2D( {
      .Format    = m_SwapchainFormat,
      .Width     = m_WindowWidth,
      .Height    = m_WindowHeight,
      .Usage     = TextureUsage::kRenderTarget,
      .MipLevels = MipLevels::kBase,
  } );
  m_DepthTexture  = m_RenderDevice->CreateTexture2D( {
       .Format    = DXGI_FORMAT_D32_FLOAT,
       .Width     = m_WindowWidth,
       .Height    = m_WindowHeight,
       .Usage     = TextureUsage::kDepthSample,
       .MipLevels = MipLevels::kBase,
  } );

  m_PrevMouseX    = g_Input.MousePosX;
  m_PrevMouseY    = g_Input.MousePosY;

  m_RenderQuery   = m_World.GetECS().query<WorldTransform const, Mesh const, Geometry const, Material const>();
}

void Ember::BasicApp::Update()
{
  ZoneScoped;

  m_PerfCounter->Tick();

  double const              avg_delta_ms   = m_PerfCounter->GetAvgFrameTime();
  double const              avg_fps        = 1000.0f / avg_delta_ms;

  auto&                     pipeline_stats = m_PerfCounter->GetPipelineStats();

  DirectX::FXMVECTOR const& cam_pos        = m_Camera->GetPosition();
  float const               pitch          = m_Camera->GetPitch();
  float const               yaw            = m_Camera->GetYaw();
  swprintf_s(
      m_SprintfBuffer,
      L"Ember %ux%u"
      L" | frame time: %.2lf ms (%.2lf fps)"
      L"| MeshDraws: %llu, Primitives: %llu, MeshletCount: %llu "
      L"| Camera: %.2f %.2f %.2f @ %.2f %.2f",
      m_WindowWidth,
      m_WindowHeight,
      avg_delta_ms,
      avg_fps,
      pipeline_stats.ASInvocations,
      pipeline_stats.MSPrimitives,
      pipeline_stats.MSInvocations,
      cam_pos.m128_f32[0],
      cam_pos.m128_f32[1],
      cam_pos.m128_f32[2],
      yaw,
      pitch );

  SetWindowText( m_WindowHandle, m_SprintfBuffer );

  float const delta_seconds = ( float )m_PerfCounter->GetDeltaMilliSeconds() * 0.001f;

  m_TextureLoader->Update();

  float const mouse_dx = ( ( float )g_Input.MousePosX - ( float )m_PrevMouseX ) / ( float )m_WindowWidth;
  float const mouse_dy = ( ( float )g_Input.MousePosY - ( float )m_PrevMouseY ) / ( float )m_WindowHeight;
  m_PrevMouseX         = g_Input.MousePosX;
  m_PrevMouseY         = g_Input.MousePosY;

  if ( g_Input.IsRightMouseDown )
    m_Camera->SetYawPitch(
        m_Camera->GetYaw() - DirectX::XM_PI * mouse_dx, m_Camera->GetPitch() - DirectX::XM_PIDIV2 * mouse_dy );

  if ( g_Input.IsPressed( 'R' ) )
  {
    m_Camera->LocalTranslate( -5 * delta_seconds, 0, 0 );
  }
  if ( g_Input.IsPressed( 'F' ) )
  {
    m_Camera->LocalTranslate( 0, 0, -5 * delta_seconds );
  }
  if ( g_Input.IsPressed( 'S' ) )
  {
    m_Camera->LocalTranslate( 0, 0, 5 * delta_seconds );
  }
  if ( g_Input.IsPressed( 'T' ) )
  {
    m_Camera->LocalTranslate( 5 * delta_seconds, 0, 0 );
  }

  m_World.GetECS().each(
      [&]( LocalTransform& lt, RotatingModel const& rm )
      {
        lt.Rotation = DirectX::XMQuaternionMultiply(
            lt.Rotation,
            DirectX::XMQuaternionRotationAxis(
                DirectX::XMVectorSet( 0.0f, 1.0f, 0.0f, 0.0f ),
                DirectX::XMConvertToRadians( rm.Speed ) * delta_seconds ) );
      } );

  m_World.Update( delta_seconds );

  g_Input.Update();
}

void Ember::BasicApp::RenderScene(
    ID3D12GraphicsCommandList6* command_list, DrawList::Info const& draw_list_info, uint32_t frame_idx )
{
  ZoneScoped;

  CBVHandle const camera_cbv                    = m_Camera->GetLastUpdatedBuffer();
  auto const [omni_light_srv, dir_light_srv]    = m_LightManager->PrepareFrame( frame_idx );
  SRVHandle const                materials_srv  = m_MaterialManager->PrepareFrame();

  DirectX::BoundingFrustum const camera_frustum = m_Camera->GetLastUpdatedFrustum();

  // Viewport and scissor
  D3D12_VIEWPORT const viewport = {
    .TopLeftX = 0,
    .TopLeftY = 0,
    .Width    = ( FLOAT )m_WindowWidth,
    .Height   = ( FLOAT )m_WindowHeight,
    .MinDepth = 0,
    .MaxDepth = 1,
  };
  D3D12_RECT const scissor = {
    .left   = 0,
    .top    = 0,
    .right  = ( LONG )m_WindowWidth,
    .bottom = ( LONG )m_WindowHeight,
  };

  command_list->SetPipelineState( m_MainPipeline.Get() );
  command_list->SetGraphicsRootSignature( m_RootSignature.Get() );
  command_list->IASetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST );

  auto bindless_desc_heaps = m_RenderDevice->GetBindlessDescriptorHeaps();
  command_list->SetDescriptorHeaps( CountOf( bindless_desc_heaps ), DataOf( bindless_desc_heaps ) );
  command_list->RSSetViewports( 1, &viewport );
  command_list->RSSetScissorRects( 1, &scissor );
  m_RenderTargetManager->OMSetRenderTargets( command_list, 1, &m_RenderTexture, &m_DepthTexture );

  PerFrameConstants const constants = {
    .MaterialsBuffer      = materials_srv,
    .Camera               = camera_cbv,
    .OmniLightBuffer      = omni_light_srv,
    .OmniLightCount       = m_LightManager->GetOmniLightCount(),
    .OmniLightShadowCount = m_LightManager->GetShadowingOmniLightCount(),
    .DirLightBuffer       = dir_light_srv,
    .DirLightCount        = m_LightManager->GetDirLightCount(),
    .DirLightShadowCount  = m_LightManager->GetShadowingDirLightCount(),
  };

  command_list->SetGraphicsRoot32BitConstants( 1, sizeof( PerFrameConstants ) / 4, &constants, 0 );
  command_list->SetGraphicsRoot32BitConstants( 2, sizeof( Environment::GpuRepr ) / 4, &m_Environment->Repr(), 0 );

  command_list->SetGraphicsRoot32BitConstants( 0, sizeof( DrawList::Info ) / 4, &draw_list_info, 0 );
  command_list->DispatchMesh( draw_list_info.DrawCount, 1, 1 );
}

void Ember::BasicApp::Render()
{
  ZoneScoped;

  ID3D12Resource*      backbuffer   = m_RenderDevice->GetCurrentBackbuffer();
  Context::CommandList command_list = m_RenderDevice->GetGraphicsCommandList();
  uint32_t const       frame_idx    = m_RenderDevice->GetCurrentFrameIndex();

  // All resources for this frame are guaranteed to be available for CPU modification at this time.
  // Clear Backbuffer

  m_Camera->PrepareFrame( frame_idx );

  m_DrawList.Clear();

  {
    ZoneScopedN( "Upload Transforms" );
    m_RenderQuery.each(
        [&]( WorldTransform const& wt, Mesh const& mesh, Geometry const& geometry, Material const& material )
        { m_DrawList.PushDraw( wt, mesh, geometry, material ); } );
  }
  CD3DX12_RESOURCE_BARRIER top_of_renderpass_barriers[] = {
    CD3DX12_RESOURCE_BARRIER::Transition( backbuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST ),
  };

  DrawList::Info const draw_list_info = m_DrawList.PrepareFrame( frame_idx );

  m_PerfCounter->UpdatePipelineStats( frame_idx );
  m_PerfCounter->BeginQuery( command_list.Get(), frame_idx );

  command_list->ResourceBarrier( CountOf( top_of_renderpass_barriers ), DataOf( top_of_renderpass_barriers ) );
  m_TextureLoader->FlushBarriers( command_list.Get() );

  m_LightManager->RenderAllShadows( command_list.Get(), draw_list_info, *m_RenderTargetManager, *m_Camera, frame_idx );

  FLOAT constexpr kBlack[4] = {};
  m_RenderTargetManager->ClearRenderTargetView( command_list.Get(), m_RenderTexture, kBlack );
  m_RenderTargetManager->ClearDepthStencilView( command_list.Get(), m_DepthTexture, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0 );

  RenderScene( command_list.Get(), draw_list_info, frame_idx );

  command_list->SetPipelineState( m_BackgroundPipeline.Get() );
  command_list->DispatchMesh( 1, 1, 1 );

  CD3DX12_RESOURCE_BARRIER post_render_barriers[] = {
    CD3DX12_RESOURCE_BARRIER::Transition(
        m_RenderTexture.GetTexture(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE ),
  };
  command_list->ResourceBarrier( CountOf( post_render_barriers ), DataOf( post_render_barriers ) );

  command_list->CopyResource( backbuffer, m_RenderTexture.GetTexture() );

  CD3DX12_RESOURCE_BARRIER bottom_of_renderpass_barriers[] = {
    CD3DX12_RESOURCE_BARRIER::Transition( backbuffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT ),
    CD3DX12_RESOURCE_BARRIER::Transition(
        m_RenderTexture.GetTexture(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET ),
  };
  command_list->ResourceBarrier( CountOf( bottom_of_renderpass_barriers ), DataOf( bottom_of_renderpass_barriers ) );

  m_PerfCounter->EndQuery( command_list.Get(), frame_idx );

  m_RenderDevice->ExecuteCommandList( std::move( command_list ) );

  m_RenderDevice->Present();
}

void Ember::BasicApp::UnloadContent()
{}

void Ember::BasicApp::Resize()
{
  RECT rect;
  ::GetWindowRect( m_WindowHandle, &rect );

  m_WindowWidth  = rect.right - rect.left;
  m_WindowHeight = rect.bottom - rect.top;

  m_RenderDevice->ResizeSwapchain( m_WindowWidth, m_WindowHeight );
  m_RenderTexture = m_RenderDevice->CreateTexture2D( {
      .Format    = m_SwapchainFormat,
      .Width     = m_WindowWidth,
      .Height    = m_WindowHeight,
      .Usage     = TextureUsage::kRenderTarget,
      .MipLevels = MipLevels::kBase,
  } );
  m_DepthTexture  = m_RenderDevice->CreateTexture2D( {
       .Format    = DXGI_FORMAT_D32_FLOAT,
       .Width     = m_WindowWidth,
       .Height    = m_WindowHeight,
       .Usage     = TextureUsage::kDepthSample,
       .MipLevels = MipLevels::kBase,
  } );

  m_Camera->SetAspectRatio( ( float )m_WindowWidth / ( float )m_WindowHeight );
}
