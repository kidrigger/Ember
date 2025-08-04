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

struct PerFrameConstants
{
  Ember::CBVHandle Camera;
  Ember::SRVHandle PointLights;
  uint32_t         PointLightCount;
};

Ember::BasicApp::BasicApp(
    HWND const window_handle, std::unique_ptr<RenderDevice> render_device, std::unique_ptr<PerfCounter> perf_counter )
  : IApp{ nullptr }
  , m_WindowHandle{ window_handle }
  , m_RenderDevice{ std::move( render_device ) }
  , m_PerfCounter{ std::move( perf_counter ) }
{
  m_TextureLoader = std::make_unique_for_overwrite<TextureLoader>();
  TextureLoader::Create( m_TextureLoader.get(), m_RenderDevice.get(), 3 );

  m_ModelLoader = std::make_unique<ModelLoader>( m_RenderDevice.get(), &m_World, m_TextureLoader.get() );
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

void Ember::BasicApp::Camera::SetProjection( DirectX::FXMMATRIX& proj )
{
  Projection = proj;
  InvProj    = XMMatrixInverse( nullptr, proj );
}

void Ember::BasicApp::Camera::SetView( DirectX::FXMMATRIX& view )
{
  View    = view;
  InvView = XMMatrixInverse( nullptr, view );
}

void Ember::BasicApp::Environment::InitRepr()
{
  Repr.Skybox            = Skybox.GetSRVHandle();
  Repr.DiffuseIrradiance = DiffuseIrradiance.GetSRVHandle();
  Repr.Prefilter         = Prefilter.GetSRVHandle();
  Repr.BrdfLUT           = BrdfLUT.GetSRVHandle();
}

void Ember::BasicApp::SetupRenderPipeline()
{
  ComPtr<ID3DBlob> vertex_shader_blob;
  ERR_ABORT( D3DReadFileToBlob( L"TriangleVS.cso", &vertex_shader_blob ) );
  ComPtr<ID3DBlob> pixel_shader_blob;
  ERR_ABORT( D3DReadFileToBlob( L"TrianglePS.cso", &pixel_shader_blob ) );
  ComPtr<ID3DBlob> bg_vertex_shader_blob;
  ERR_ABORT( D3DReadFileToBlob( L"BackgroundVS.cso", &bg_vertex_shader_blob ) );
  ComPtr<ID3DBlob> bg_pixel_shader_blob;
  ERR_ABORT( D3DReadFileToBlob( L"BackgroundPS.cso", &bg_pixel_shader_blob ) );

  ComPtr<ID3D12Device2>             device = m_RenderDevice->GetDevice();

  D3D12_FEATURE_DATA_ROOT_SIGNATURE feature_data;
  feature_data.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
  if ( FAILED( device->CheckFeatureSupport( D3D12_FEATURE_ROOT_SIGNATURE, &feature_data, sizeof( feature_data ) ) ) )
  {
    feature_data.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
  }

  CD3DX12_STATIC_SAMPLER_DESC static_sampler_desc[] = {
    CD3DX12_STATIC_SAMPLER_DESC{ 0 },
    CD3DX12_STATIC_SAMPLER_DESC{ 1,
                                D3D12_FILTER_ANISOTROPIC, D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
                                D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP },
  };

  D3D12_ROOT_SIGNATURE_FLAGS const root_signature_flags =
      D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
      D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED |
      D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED | D3D12_ROOT_SIGNATURE_FLAG_DENY_MESH_SHADER_ROOT_ACCESS |
      D3D12_ROOT_SIGNATURE_FLAG_DENY_AMPLIFICATION_SHADER_ROOT_ACCESS |
      D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
      D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS | D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS;

  CD3DX12_ROOT_PARAMETER1 root_parameters[4];
  root_parameters[0].InitAsConstants( sizeof( WorldTransform ) / 4, 0 );
  root_parameters[1].InitAsConstants( sizeof( Material::GpuRepr ) / 4, 1 );
  root_parameters[2].InitAsConstants( sizeof( PerFrameConstants ) / 4, 2 );
  root_parameters[3].InitAsConstants( sizeof( Environment::GpuRepr ) / 4, 3 );

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
  rasterizer_desc.CullMode = D3D12_CULL_MODE_BACK;

  CD3DX12_DEPTH_STENCIL_DESC depth_stencil_desc{ D3D12_DEFAULT };
  depth_stencil_desc.DepthEnable       = true;
  depth_stencil_desc.DepthFunc         = D3D12_COMPARISON_FUNC_LESS;

  D3D12_INPUT_LAYOUT_DESC input_layout = {
    .pInputElementDescs = DataOf( Vertex::kInputElementDesc ),
    .NumElements        = CountOf( Vertex::kInputElementDesc ),
  };

  struct MainPipelineStream
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

  MainPipelineStream pipeline_stream = {
    .InputLayout           = input_layout,
    .RootSignature         = m_RootSignature.Get(),
    .PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
    .VS                    = CD3DX12_SHADER_BYTECODE( vertex_shader_blob.Get() ),
    .PS                    = CD3DX12_SHADER_BYTECODE( pixel_shader_blob.Get() ),
    .Rasterizer            = rasterizer_desc,
    .RTVFormats            = rtv_formats,
    .DSVFormat             = DXGI_FORMAT_D32_FLOAT,
  };

  struct BackgroundPipelineStream
  {
    CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE        RootSignature;
    CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY    PrimitiveTopologyType;
    CD3DX12_PIPELINE_STATE_STREAM_VS                    VS;
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
    .VS                    = CD3DX12_SHADER_BYTECODE( bg_vertex_shader_blob.Get() ),
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
  auto camera_position = DirectX::XMVectorSet( 0.0f, 0.0f, 2.0f, 1.0f );

  m_Camera.SetProjection( DirectX::XMMatrixPerspectiveFovLH(
      DirectX::XMConvertToRadians( 70.0f ), ( float )m_WindowWidth / ( float )m_WindowHeight, 0.1f, 100.0f ) );
  m_Camera.SetView( DirectX::XMMatrixLookAtLH(
      camera_position,
      DirectX::XMVectorSet( 0.0f, 0.0f, 0.0f, 1.0f ),
      DirectX::XMVectorSet( 0.0f, 1.0f, 0.0f, 0.0f ) ) );
  m_Camera.Position = camera_position;

  for ( auto& camera_buffer : m_CameraBuffer )
  {
    camera_buffer = m_RenderDevice->CreateConstantBuffer( sizeof( m_Camera ) );
    camera_buffer.Write( 0, sizeof( m_Camera ), &m_Camera );
  }

  // Setup Lights
  m_PointLightCount = 0;
  m_PointLights[0]  = {
     .Position    = { 1.0f, 1.0f, -1.0f },
     .Range       = 15.0f,
     .Color       = Color32::White(),
     .Intensity   = 5.0f,
     .Attenuation = 1.0f,
     .Padding0    = 1.0f,
  };
  m_PointLights[1] = {
    .Position    = { -1.0f, 1.0f, -1.0f },
    .Range       = 15.0f,
    .Color       = Color32::Green(),
    .Intensity   = 5.0f,
    .Attenuation = 1.0f,
    .Padding0    = 1.0f,
  };
  m_PointLights[2] = {
    .Position    = { 0.0f, 1.0f, 0.0f },
    .Range       = 15.0f,
    .Color       = Color32::Red(),
    .Intensity   = 5.0f,
    .Attenuation = 1.0f,
    .Padding0    = 1.0f,
  };
  m_PointLightDirty = 0;

  for ( auto& point_light_buffer : m_PointLightBuffer )
  {
    point_light_buffer = m_RenderDevice->CreateStorageBuffer( ByteSizeOf( m_PointLights ), sizeof( PointLight ) );
  }

  // Setup Scene Geometry
  // RotModel* rm    = m_World.CreateObject<RotModel>();
  Model* model = m_ModelLoader->TryLoadModel( "DamagedHelmet.glb" );
  // model->SetLocalScale( DirectX::XMVectorSet( -40.0f, 40.0f, 40.0f, 1.0f ) );
  ASSERT( model );
  // rm->AddChild( model );

  // Setup Environment
  uint32_t constexpr kEnvCubeSide       = 256;
  uint32_t constexpr kDiffuseCubeSide   = 256;
  uint32_t constexpr kPrefilterCubeSide = 512;
  uint32_t constexpr kPrefilterMaxLoD   = 5;
  uint32_t constexpr kBrdfLutSize       = 512;

  //
  m_Environment.Device = m_RenderDevice.get();
  m_Environment.Skybox = m_RenderDevice->CreateTextureCube( {
      .Format    = DXGI_FORMAT_R11G11B10_FLOAT,
      .Side      = kEnvCubeSide,
      .Usage     = TextureUsage::kReadWrite,
      .MipLevels = MipLevels::kBase,
  } );
  m_Environment.Skybox.SetName( L"Skybox" );

  m_Environment.DiffuseIrradiance = m_RenderDevice->CreateTextureCube( {
      .Format    = DXGI_FORMAT_R11G11B10_FLOAT,
      .Side      = kDiffuseCubeSide,
      .Usage     = TextureUsage::kReadWrite,
      .MipLevels = MipLevels::kBase,
  } );
  m_Environment.DiffuseIrradiance.SetName( L"Diffuse Irradiance Map" );

  m_Environment.Prefilter = m_RenderDevice->CreateTextureCube( {
      .Format    = DXGI_FORMAT_R11G11B10_FLOAT,
      .Side      = kPrefilterCubeSide,
      .Usage     = TextureUsage::kReadWrite,
      .MipLevels = kPrefilterMaxLoD + 1, // accounting for mip0
  } );
  m_Environment.Prefilter.SetName( L"Prefiltered Cube" );

  m_Environment.BrdfLUT = m_RenderDevice->CreateTexture2D( {
      .Format    = DXGI_FORMAT_R16G16_FLOAT,
      .Width     = kBrdfLutSize,
      .Height    = kBrdfLutSize,
      .Usage     = TextureUsage::kReadWrite,
      .MipLevels = MipLevels::kBase,
  } );
  m_Environment.BrdfLUT.SetName( L"BRDF LUT" );

  {
    Texture environment;
    ASSERT( m_TextureLoader->TryLoadTexture( &environment, "PhotoStudioLoft.hdr" ) );
    m_RenderDevice->WaitOn( m_TextureLoader->EndBatch() );

    struct EnvRootConstant
    {
      SRVHandle InputTextureHandle;
      UAVHandle OutputTextureHandle;
      uint32_t  CubeSide;
    };

    struct PrefilterConstant
    {
      SRVHandle Skybox;
      uint32_t  SkyboxSide;
      UAVHandle OutputTextureHandle;
      uint32_t  OutputSide;
      float     Roughness;
    };

    struct BrdfLUTConstant
    {
      UAVHandle OutputTextureHandle;
      uint32_t  Width;
      uint32_t  Height;
    };

    CD3DX12_ROOT_PARAMETER1 root_parameters[1];
    root_parameters[0].InitAsConstants( 5, 0 );

    CD3DX12_STATIC_SAMPLER_DESC static_sampler_desc[] = {
      CD3DX12_STATIC_SAMPLER_DESC{ 0 },
    };

    D3D12_ROOT_SIGNATURE_FLAGS const root_signature_flags =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
        D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED |
        D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_MESH_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_AMPLIFICATION_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS;

    uint32_t constexpr kThreadGroupX = 16;
    uint32_t constexpr kThreadGroupY = 16;
    uint32_t constexpr kThreadGroupZ = 1;

    ComPtr<ID3DBlob> eqrect_to_cube_shader;
    ERR_ABORT( D3DReadFileToBlob( L"EqrectToCube.cso", &eqrect_to_cube_shader ) );

    ComPtr<ID3DBlob> diffuse_irradiance_shader;
    ERR_ABORT( D3DReadFileToBlob( L"DiffuseIrradiance.cso", &diffuse_irradiance_shader ) );

    ComPtr<ID3DBlob> prefilter_shader;
    ERR_ABORT( D3DReadFileToBlob( L"Prefilter.cso", &prefilter_shader ) );

    ComPtr<ID3DBlob> brdf_lut_shader;
    ERR_ABORT( D3DReadFileToBlob( L"BrdfLUT.cso", &brdf_lut_shader ) );

    ComPtr<ID3D12Device2> device = m_RenderDevice->GetDevice();

    Context               context;
    Context::Create( &context, device, D3D12_COMMAND_LIST_TYPE_COMPUTE );

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC versioned_root_signature_desc;
    versioned_root_signature_desc.Init_1_1(
        CountOf( root_parameters ),
        DataOf( root_parameters ),
        CountOf( static_sampler_desc ),
        DataOf( static_sampler_desc ),
        root_signature_flags );

    D3D12_FEATURE_DATA_ROOT_SIGNATURE feature_data;
    feature_data.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
    if ( FAILED( device->CheckFeatureSupport( D3D12_FEATURE_ROOT_SIGNATURE, &feature_data, sizeof( feature_data ) ) ) )
    {
      feature_data.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
    }

    ComPtr<ID3DBlob> root_signature_blob;
    ComPtr<ID3DBlob> error_blob;
    ERR_ABORT( D3DX12SerializeVersionedRootSignature(
        &versioned_root_signature_desc, feature_data.HighestVersion, &root_signature_blob, &error_blob ) );

    ComPtr<ID3D12RootSignature> root_signature;
    ERR_ABORT( device->CreateRootSignature(
        0,
        root_signature_blob->GetBufferPointer(),
        root_signature_blob->GetBufferSize(),
        IID_PPV_ARGS( &root_signature ) ) );

    struct EnvPipelineStream
    {
      CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE RootSignature;
      CD3DX12_PIPELINE_STATE_STREAM_CS             ComputeShader;
    };

    EnvPipelineStream pipeline_stream{
      .RootSignature = root_signature.Get(),
    };

    D3D12_PIPELINE_STATE_STREAM_DESC desc{
      .SizeInBytes                   = sizeof pipeline_stream,
      .pPipelineStateSubobjectStream = &pipeline_stream,
    };

    pipeline_stream.ComputeShader = CD3DX12_SHADER_BYTECODE( eqrect_to_cube_shader.Get() );
    ComPtr<ID3D12PipelineState> eqrect_to_cube_pipeline;
    ERR_ABORT( device->CreatePipelineState( &desc, IID_PPV_ARGS( &eqrect_to_cube_pipeline ) ) );
    ERR_ABORT( eqrect_to_cube_pipeline->SetName( L"Eqrect -> Cube Pipeline" ) );

    pipeline_stream.ComputeShader = CD3DX12_SHADER_BYTECODE( diffuse_irradiance_shader.Get() );
    ComPtr<ID3D12PipelineState> diffuse_irradiance_pipeline;
    ERR_ABORT( device->CreatePipelineState( &desc, IID_PPV_ARGS( &diffuse_irradiance_pipeline ) ) );
    ERR_ABORT( diffuse_irradiance_pipeline->SetName( L"Diffuse Irradiance Pipeline" ) );

    pipeline_stream.ComputeShader = CD3DX12_SHADER_BYTECODE( prefilter_shader.Get() );
    ComPtr<ID3D12PipelineState> prefilter_pipeline;
    ERR_ABORT( device->CreatePipelineState( &desc, IID_PPV_ARGS( &prefilter_pipeline ) ) );
    ERR_ABORT( prefilter_pipeline->SetName( L"Prefilter Pipeline" ) );

    pipeline_stream.ComputeShader = CD3DX12_SHADER_BYTECODE( brdf_lut_shader.Get() );
    ComPtr<ID3D12PipelineState> brdf_lut_pipeline;
    ERR_ABORT( device->CreatePipelineState( &desc, IID_PPV_ARGS( &brdf_lut_pipeline ) ) );
    ERR_ABORT( brdf_lut_pipeline->SetName( L"BRDF LUT Pipeline" ) );

    D3D12_RESOURCE_DESC prefilter_desc = m_Environment.Prefilter.GetTexture()->GetDesc();
    ASSERT( prefilter_desc.MipLevels == kPrefilterMaxLoD + 1 /* Accounting for mip0 */ );

    std::vector<UAVHandle> prefilter_write_handles;
    for ( uint32_t i = 0; i <= kPrefilterMaxLoD; i++ )
    {
      auto uav_desc = CD3DX12_UNORDERED_ACCESS_VIEW_DESC::Tex2DArray( prefilter_desc.Format, ( UINT )-1, 0, i );
      prefilter_write_handles.push_back(
          m_RenderDevice->CreateBindlessHandle( m_Environment.Prefilter.GetTexture(), uav_desc ) );
    }

    auto            desc_heaps = m_RenderDevice->GetBindlessDescriptorHeaps();

    EnvRootConstant env_cube_root_constant{
      .InputTextureHandle  = environment.GetSRVHandle(),
      .OutputTextureHandle = m_Environment.Skybox.GetUAVHandle(),
      .CubeSide            = kEnvCubeSide,
    };

    EnvRootConstant diffuse_irradiance_root_constant{
      .InputTextureHandle  = m_Environment.Skybox.GetSRVHandle(),
      .OutputTextureHandle = m_Environment.DiffuseIrradiance.GetUAVHandle(),
      .CubeSide            = kDiffuseCubeSide,
    };

    PrefilterConstant prefilter_constant{
      .Skybox              = m_Environment.Skybox.GetSRVHandle(),
      .SkyboxSide          = kEnvCubeSide,
      .OutputTextureHandle = m_Environment.Prefilter.GetUAVHandle(),
      .OutputSide          = kPrefilterCubeSide,
      .Roughness           = 0.0f,
    };

    BrdfLUTConstant brdf_lut_constant{
      .OutputTextureHandle = m_Environment.BrdfLUT.GetUAVHandle(),
      .Width               = kBrdfLutSize,
      .Height              = kBrdfLutSize,
    };

    auto command_list = context.GetCommandList();

    command_list->SetComputeRootSignature( root_signature.Get() );
    command_list->SetDescriptorHeaps( CountOf( desc_heaps ), DataOf( desc_heaps ) );

    command_list->SetPipelineState( eqrect_to_cube_pipeline.Get() );
    command_list->SetComputeRoot32BitConstants( 0, sizeof( EnvRootConstant ) / 4, &env_cube_root_constant, 0 );
    command_list->Dispatch( kEnvCubeSide / kThreadGroupX, kEnvCubeSide / kThreadGroupY, 6 / kThreadGroupZ );

    byte                                buffer[2048];
    std::pmr::monotonic_buffer_resource mbr{ DataOf( buffer ), ByteSizeOf( buffer ), std::pmr::null_memory_resource() };
    ResourceTracker                     tracker{ m_RenderDevice.get(), &mbr };
    {
      auto barrier = CD3DX12_RESOURCE_BARRIER::UAV( m_Environment.Skybox.GetTexture() );
      command_list->ResourceBarrier( 1, &barrier );
    }

    ASSERT( m_TextureLoader->TryGenerateMipMapCube( command_list.Get(), &m_Environment.Skybox, &tracker ) );

    {
      auto barrier = CD3DX12_RESOURCE_BARRIER::UAV( m_Environment.Skybox.GetTexture() );
      command_list->ResourceBarrier( 1, &barrier );
    }

    command_list->SetComputeRootSignature( root_signature.Get() );
    command_list->SetPipelineState( diffuse_irradiance_pipeline.Get() );
    command_list->SetComputeRoot32BitConstants(
        0, sizeof( EnvRootConstant ) / 4, &diffuse_irradiance_root_constant, 0 );
    command_list->Dispatch( kDiffuseCubeSide / kThreadGroupX, kDiffuseCubeSide / kThreadGroupY, 6 / kThreadGroupZ );

    for ( uint32_t i = 0; i <= kPrefilterMaxLoD; i++ )
    {
      prefilter_constant.OutputTextureHandle = prefilter_write_handles[i];
      prefilter_constant.Roughness           = ( float )i / ( float )kPrefilterMaxLoD;

      command_list->SetPipelineState( prefilter_pipeline.Get() );
      command_list->SetComputeRoot32BitConstants( 0, sizeof( PrefilterConstant ) / 4, &prefilter_constant, 0 );
      command_list->Dispatch(
          std::max<uint32_t>( prefilter_constant.OutputSide / kThreadGroupX, 1 ),
          std::max<uint32_t>( prefilter_constant.OutputSide / kThreadGroupY, 1 ),
          6 / kThreadGroupZ );

      prefilter_constant.OutputSide = std::max<uint32_t>( prefilter_constant.OutputSide / 2, 1 );
    }

    command_list->SetPipelineState( brdf_lut_pipeline.Get() );
    command_list->SetComputeRoot32BitConstants( 0, sizeof( BrdfLUTConstant ) / 4, &brdf_lut_constant, 0 );
    command_list->Dispatch( kBrdfLutSize / kThreadGroupX, kBrdfLutSize / kThreadGroupY, 1 );

    Context::Receipt receipt = context.Submit( std::move( command_list ) );
    context.WaitOn( receipt );

    for ( auto& uav_handle : prefilter_write_handles )
    {
      m_RenderDevice->FreeHandle( uav_handle );
    }

    tracker.Clear( nullptr );
  }
  m_Environment.InitRepr();

  SetupRenderPipeline();

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
  m_TextureLoader->Update();

  /*
  m_Camera.Position = DirectX::XMVector3Rotate(
      m_Camera.Position,
      DirectX::XMQuaternionRotationRollPitchYaw( 0.0f, DirectX::XMConvertToRadians( 10 ) * delta_seconds, 0.0f ) );

  m_Camera.SetView( DirectX::XMMatrixLookAtLH(
      m_Camera.Position,
      DirectX::XMVectorSet( 0.0f, 0.0f, 0.0f, 1.0f ),
      DirectX::XMVectorSet( 0.0f, 1.0f, 0.0f, 0.0f ) ) );
      */

  m_World.Update( delta_seconds );
}

void Ember::BasicApp::Render()
{
  m_RenderQueue.Clear();
  m_World.Render( &m_RenderQueue );

  ID3D12Resource*      backbuffer         = m_RenderDevice->GetCurrentBackbuffer();
  Context::CommandList command_list       = m_RenderDevice->GetGraphicsCommandList();
  uint32_t             frame_idx          = m_RenderDevice->GetCurrentFrameIndex();
  Buffer*              camera_buffer      = &m_CameraBuffer[frame_idx];
  Buffer*              point_light_buffer = &m_PointLightBuffer[frame_idx];

  // All resources for this frame are guaranteed to be available for CPU modification at this time.
  camera_buffer->Write( 0, sizeof( Camera ), &m_Camera );

  if ( ( m_PointLightDirty-- ) > 0 )
  {
    point_light_buffer->Write( 0, ByteSizeOf( m_PointLights ), DataOf( m_PointLights ) );
  }

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

  // Clear Backbuffer
  CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
      backbuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET );

  command_list->ResourceBarrier( 1, &barrier );
  m_ModelLoader->FlushBarriers( command_list.Get() );
  m_TextureLoader->FlushBarriers( command_list.Get() );

  FLOAT constexpr cornflower_blue[]       = { 0.4f, 0.6f, 0.9f, 1.0f };
  CD3DX12_CPU_DESCRIPTOR_HANDLE const rtv = m_RenderDevice->GetCurrentRTVCpuDescriptorHandle();
  CD3DX12_CPU_DESCRIPTOR_HANDLE const dsv = m_RenderDevice->GetCurrentDSVCpuDescriptorHandle();
  command_list->ClearRenderTargetView( rtv, cornflower_blue, 0, nullptr );
  command_list->ClearDepthStencilView( dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr );

  command_list->SetPipelineState( m_MainPipeline.Get() );
  command_list->SetGraphicsRootSignature( m_RootSignature.Get() );
  command_list->IASetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST );

  auto bindless_desc_heaps = m_RenderDevice->GetBindlessDescriptorHeaps();
  command_list->SetDescriptorHeaps( CountOf( bindless_desc_heaps ), DataOf( bindless_desc_heaps ) );
  command_list->RSSetViewports( 1, &viewport );
  command_list->RSSetScissorRects( 1, &scissor );
  command_list->OMSetRenderTargets( 1, &rtv, FALSE, &dsv );

  PerFrameConstants constants = {
    .Camera          = camera_buffer->GetCBVHandle(),
    .PointLights     = point_light_buffer->GetSRVHandle(),
    .PointLightCount = m_PointLightCount,
  };

  command_list->SetGraphicsRoot32BitConstants( 2, sizeof( PerFrameConstants ) / 4, &constants, 0 );
  command_list->SetGraphicsRoot32BitConstants( 3, sizeof( Environment::GpuRepr ) / 4, &m_Environment.Repr, 0 );

  size_t const count = m_RenderQueue.Count();
  for ( size_t i = 0; i < count; ++i )
  {
    command_list->IASetIndexBuffer( &m_RenderQueue.Meshes[i]->IndexBuffer.GetIndexBufferView() );
    command_list->IASetVertexBuffers( 0, 1, &m_RenderQueue.Meshes[i]->VertexBuffer.GetVertexBufferView() );

    command_list->SetGraphicsRoot32BitConstants( 0, sizeof( WorldTransform ) / 4, &m_RenderQueue.Transforms[i], 0 );
    command_list->SetGraphicsRoot32BitConstants(
        1, sizeof( Material::GpuRepr ) / 4, &m_RenderQueue.Materials[i]->Repr, 0 );

    command_list->DrawIndexedInstanced(
        m_RenderQueue.Primitives[i].IndexCount,
        1,
        m_RenderQueue.Primitives[i].FirstIndex,
        m_RenderQueue.Primitives[i].FirstVertex,
        0 );
  }

  command_list->SetPipelineState( m_BackgroundPipeline.Get() );
  command_list->DrawInstanced( 3, 1, 0, 0 );

  barrier = CD3DX12_RESOURCE_BARRIER::Transition(
      backbuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT );
  command_list->ResourceBarrier( 1, &barrier );

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

  m_Camera.SetProjection( DirectX::XMMatrixPerspectiveFovLH(
      DirectX::XMConvertToRadians( 70.0f ), ( float )m_WindowWidth / ( float )m_WindowHeight, 0.1f, 100.0f ) );
}
