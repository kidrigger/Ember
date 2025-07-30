#include "BasicApp.hpp"

#include <cstdint>
#include <utility>

#include "ModelLoader.hpp"
#include "RenderDevice.hpp"
#include "TextureLoader.hpp"
#include "Util/DataUtil.hpp"
#include "Util/HelperUtils.hpp"
#include "Util/PerfCounter.hpp"

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

class RotModel final : public Ember::Node
{
public:
  explicit RotModel( Object* parent, allocator_type const& allocator = {} ) : Node{ parent, allocator }
  {}

  void Update( float const delta_seconds ) override
  {
    auto transform = GetLocalTransform();
    transform      = XMMatrixMultiply(
        DirectX::XMMatrixRotationY( DirectX::XMConvertToRadians( 20.0f ) * delta_seconds ), transform );
    SetLocalTransform( transform );

    Node::Update( delta_seconds );
  }
};

Ember::BasicApp::BasicApp(
    HWND const window_handle, std::unique_ptr<RenderDevice> render_device, std::unique_ptr<PerfCounter> perf_counter )
  : IApp{ nullptr }
  , m_WindowHandle{ window_handle }
  , m_RenderDevice{ std::move( render_device ) }
  , m_PerfCounter{ std::move( perf_counter ) }
  , m_ModelLoader{ std::make_unique<ModelLoader>( m_RenderDevice.get(), &m_World ) }
{}

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

  auto perf_counter = std::make_unique<PerfCounter>();

  new ( app ) BasicApp{
    window_handle,
    std::move( render_device ),
    std::move( perf_counter ),
  };
}

Ember::BasicApp::~BasicApp() // NOLINT(modernize-use-equals-default)
{
  m_RenderDevice->WaitIdle();
}

void Ember::BasicApp::LoadContent()
{
  ERR_ABORT( ::ShowWindow( m_WindowHandle, SW_SHOW ) );

  m_Camera = {
    .Projection = DirectX::XMMatrixPerspectiveFovLH(
        DirectX::XMConvertToRadians( 70.0f ), ( float )m_WindowWidth / ( float )m_WindowHeight, 0.1f, 100.0f ),
    .View = DirectX::XMMatrixLookAtLH(
        DirectX::XMVectorSet( 0.0f, 15.0f, -15.0f, 1.0f ),
        DirectX::XMVectorSet( 0.0f, 0.0f, 0.0f, 1.0f ),
        DirectX::XMVectorSet( 0.0f, 1.0f, 0.0f, 0.0f ) ),
  };

  m_CameraBuffer = m_RenderDevice->CreateConstantBuffer( sizeof( m_Camera ) );
  m_CameraBuffer.Write( 0, sizeof( m_Camera ), &m_Camera );

  Sampler   sampler = m_RenderDevice->CreateSampler( {
        .Filter         = D3D12_FILTER_ANISOTROPIC,
        .AddressU       = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        .AddressV       = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        .AddressW       = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        .MipLODBias     = 0.0f,
        .MaxAnisotropy  = D3D12_DEFAULT_MAX_ANISOTROPY,
        .ComparisonFunc = D3D12_COMPARISON_FUNC_NONE,
        .BorderColor    = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK,
        .MinLOD         = 0.0f,
        .MaxLOD         = INFINITY,
  } );

  RotModel* rm      = m_World.CreateObject<RotModel>();
  Model*    model   = m_ModelLoader->TryLoadModel( "BoxTextured.glb" );
  ASSERT( model );
  rm->AddChild( model );

  ComPtr<ID3DBlob> vertex_shader_blob;
  ERR_ABORT( D3DReadFileToBlob( L"TriangleVS.cso", &vertex_shader_blob ) );
  ComPtr<ID3DBlob> pixel_shader_blob;
  ERR_ABORT( D3DReadFileToBlob( L"TrianglePS.cso", &pixel_shader_blob ) );

  ComPtr<ID3D12Device2>             device = m_RenderDevice->GetDevice();

  D3D12_FEATURE_DATA_ROOT_SIGNATURE feature_data;
  feature_data.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
  if ( FAILED( device->CheckFeatureSupport( D3D12_FEATURE_ROOT_SIGNATURE, &feature_data, sizeof( feature_data ) ) ) )
  {
    feature_data.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
  }

  D3D12_ROOT_SIGNATURE_FLAGS const root_signature_flags =
      D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
      D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED |
      D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED | D3D12_ROOT_SIGNATURE_FLAG_DENY_MESH_SHADER_ROOT_ACCESS |
      D3D12_ROOT_SIGNATURE_FLAG_DENY_AMPLIFICATION_SHADER_ROOT_ACCESS |
      D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
      D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS | D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS;

  CD3DX12_ROOT_PARAMETER1 root_parameters[3];
  root_parameters[0].InitAsConstants( 1, 0, 0, D3D12_SHADER_VISIBILITY_ALL );
  root_parameters[1].InitAsConstants( sizeof( DirectX::XMMATRIX ) / 4, 1, 0, D3D12_SHADER_VISIBILITY_ALL );
  root_parameters[2].InitAsConstants( sizeof( Material::GpuRepr ) / 4, 2, 0, D3D12_SHADER_VISIBILITY_ALL );

  CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC root_signature_desc;
  root_signature_desc.Init_1_1(
      CountOf( root_parameters ), DataOf( root_parameters ), 0, nullptr, root_signature_flags );

  ComPtr<ID3DBlob> root_signature_blob;
  ComPtr<ID3DBlob> error_blob;
  ERR_ABORT( D3DX12SerializeVersionedRootSignature(
      &root_signature_desc, feature_data.HighestVersion, &root_signature_blob, &error_blob ) );

  ERR_ABORT( device->CreateRootSignature(
      0,
      root_signature_blob->GetBufferPointer(),
      root_signature_blob->GetBufferSize(),
      IID_PPV_ARGS( &m_RootSignature ) ) );

  D3D12_RT_FORMAT_ARRAY rtv_formats = {
    .NumRenderTargets = 1,
  };
  rtv_formats.RTFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;

  CD3DX12_RASTERIZER_DESC2 rasterizer_desc{ D3D12_DEFAULT };
  rasterizer_desc.CullMode                  = D3D12_CULL_MODE_NONE;

  D3D12_INPUT_ELEMENT_DESC input_elements[] = {
    {
     .SemanticName         = "POSITION",
     .SemanticIndex        = 0,
     .Format               = DXGI_FORMAT_R32G32B32_FLOAT,
     .InputSlot            = 0,
     .AlignedByteOffset    = D3D12_APPEND_ALIGNED_ELEMENT,
     .InputSlotClass       = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
     .InstanceDataStepRate = 0,
     },
    {
     .SemanticName         = "NORMAL",
     .SemanticIndex        = 0,
     .Format               = DXGI_FORMAT_R32G32B32_FLOAT,
     .InputSlot            = 0,
     .AlignedByteOffset    = D3D12_APPEND_ALIGNED_ELEMENT,
     .InputSlotClass       = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
     .InstanceDataStepRate = 0,
     },
    {
     .SemanticName         = "TANGENT",
     .SemanticIndex        = 0,
     .Format               = DXGI_FORMAT_R32G32B32A32_FLOAT,
     .InputSlot            = 0,
     .AlignedByteOffset    = D3D12_APPEND_ALIGNED_ELEMENT,
     .InputSlotClass       = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
     .InstanceDataStepRate = 0,
     },
    {
     .SemanticName         = "COLOR",
     .SemanticIndex        = 0,
     .Format               = DXGI_FORMAT_R32G32B32_FLOAT,
     .InputSlot            = 0,
     .AlignedByteOffset    = D3D12_APPEND_ALIGNED_ELEMENT,
     .InputSlotClass       = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
     .InstanceDataStepRate = 0,
     },
    {
     .SemanticName         = "TEXCOORD",
     .SemanticIndex        = 0,
     .Format               = DXGI_FORMAT_R32G32_FLOAT,
     .InputSlot            = 0,
     .AlignedByteOffset    = D3D12_APPEND_ALIGNED_ELEMENT,
     .InputSlotClass       = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
     .InstanceDataStepRate = 0,
     },
    {
     .SemanticName         = "TEXCOORD",
     .SemanticIndex        = 1,
     .Format               = DXGI_FORMAT_R32G32_FLOAT,
     .InputSlot            = 0,
     .AlignedByteOffset    = D3D12_APPEND_ALIGNED_ELEMENT,
     .InputSlotClass       = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
     .InstanceDataStepRate = 0,
     },
  };

  D3D12_INPUT_LAYOUT_DESC input_layout = {
    .pInputElementDescs = DataOf( input_elements ),
    .NumElements        = CountOf( input_elements ),
  };

  struct PipelineStateStream
  {
    CD3DX12_PIPELINE_STATE_STREAM_INPUT_LAYOUT          InputLayout;
    CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE        RootSignature;
    CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY    PrimitiveTopologyType;
    CD3DX12_PIPELINE_STATE_STREAM_VS                    VS;
    CD3DX12_PIPELINE_STATE_STREAM_PS                    PS;
    CD3DX12_PIPELINE_STATE_STREAM_RASTERIZER2           Rasterizer;
    CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS RTVFormats;
    CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL_FORMAT  DSVFormat;
  };

  PipelineStateStream pipeline_stream = {
    .InputLayout           = input_layout,
    .RootSignature         = m_RootSignature.Get(),
    .PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
    .VS                    = CD3DX12_SHADER_BYTECODE( vertex_shader_blob.Get() ),
    .PS                    = CD3DX12_SHADER_BYTECODE( pixel_shader_blob.Get() ),
    .Rasterizer            = rasterizer_desc,
    .RTVFormats            = rtv_formats,
    .DSVFormat             = DXGI_FORMAT_D32_FLOAT,
  };

  D3D12_PIPELINE_STATE_STREAM_DESC const pipeline_state_stream_desc = {
    .SizeInBytes                   = sizeof pipeline_stream,
    .pPipelineStateSubobjectStream = &pipeline_stream,
  };

  ERR_ABORT( device->CreatePipelineState( &pipeline_state_stream_desc, IID_PPV_ARGS( &m_PipelineState ) ) );

  m_DepthBuffer = m_RenderDevice->CreateDepthBuffer( m_WindowWidth, m_WindowHeight );

  m_RenderDevice->SetDepthBuffer( m_DepthBuffer );
}

void Ember::BasicApp::Update()
{
  m_PerfCounter->Tick();

  double const avg_delta_ms = m_PerfCounter->GetAvgFrameTime();
  double const avg_fps      = 1000.0f / avg_delta_ms;
  swprintf_s( m_SprintfBuffer, L"Ember | frame time: %.2lf ms (%.2lf fps)", avg_delta_ms, avg_fps );

  SetWindowText( m_WindowHandle, m_SprintfBuffer );

  float const delta_seconds = ( float )m_PerfCounter->GetDeltaMilliSeconds() * 0.001f;

  m_ModelLoader->Update();

  m_World.Update( delta_seconds );
}

void Ember::BasicApp::Render()
{
  m_RenderQueue.Clear();
  m_World.Render( &m_RenderQueue );

  ID3D12Resource*                   backbuffer   = m_RenderDevice->GetCurrentBackbuffer();
  ComPtr<ID3D12GraphicsCommandList> command_list = m_RenderDevice->GetGraphicsCommandList();

  D3D12_VIEWPORT const              viewport     = {
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

  // Clear Backbuffer
  CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
      backbuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET );

  command_list->ResourceBarrier( 1, &barrier );
  m_ModelLoader->FlushBarriers( command_list.Get() );

  FLOAT constexpr cornflower_blue[]       = { 0.4f, 0.6f, 0.9f, 1.0f };
  CD3DX12_CPU_DESCRIPTOR_HANDLE const rtv = m_RenderDevice->GetCurrentRTVCpuDescriptorHandle();
  CD3DX12_CPU_DESCRIPTOR_HANDLE const dsv = m_RenderDevice->GetCurrentDSVCpuDescriptorHandle();
  command_list->ClearRenderTargetView( rtv, cornflower_blue, 0, nullptr );
  command_list->ClearDepthStencilView( dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr );

  command_list->SetPipelineState( m_PipelineState.Get() );
  command_list->SetGraphicsRootSignature( m_RootSignature.Get() );
  command_list->IASetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST );

  auto bindless_desc_heaps = m_RenderDevice->GetBindlessDescriptorHeaps();
  command_list->SetDescriptorHeaps( CountOf( bindless_desc_heaps ), DataOf( bindless_desc_heaps ) );
  command_list->RSSetViewports( 1, &viewport );
  command_list->RSSetScissorRects( 1, &scissor );
  command_list->OMSetRenderTargets( 1, &rtv, FALSE, &dsv );

  size_t const count = m_RenderQueue.Count();
  for ( size_t i = 0; i < count; ++i )
  {
    command_list->IASetIndexBuffer( &m_RenderQueue.Meshes[i]->IndexBuffer.GetIndexBufferView() );
    command_list->IASetVertexBuffers( 0, 1, &m_RenderQueue.Meshes[i]->VertexBuffer.GetVertexBufferView() );
    command_list->SetGraphicsRoot32BitConstant( 0, ( UINT )m_CameraBuffer.GetCBVHandle(), 0 );
    command_list->SetGraphicsRoot32BitConstants(
        1, sizeof( DirectX::XMMATRIX ) / 4, &m_RenderQueue.Transforms[i].Transform, 0 );
    command_list->SetGraphicsRoot32BitConstants(
        2, sizeof( Material::GpuRepr ) / 4, &m_RenderQueue.Materials[i]->Repr, 0 );
    command_list->DrawIndexedInstanced(
        m_RenderQueue.Primitives[i].IndexCount,
        1,
        m_RenderQueue.Primitives[i].FirstIndex,
        m_RenderQueue.Primitives[i].FirstVertex,
        0 );
  }

  barrier = CD3DX12_RESOURCE_BARRIER::Transition(
      backbuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT );
  command_list->ResourceBarrier( 1, &barrier );

  ERR_ABORT( command_list->Close() );

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
  m_DepthBuffer = m_RenderDevice->CreateDepthBuffer( m_WindowWidth, m_WindowHeight );
  m_RenderDevice->SetDepthBuffer( m_DepthBuffer );
}
