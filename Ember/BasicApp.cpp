#include "BasicApp.hpp"

#include <cstdint>
#include <fstream>
#include <unordered_set>
#include <utility>

#include "AtmosphereContext.hpp"
#include "Camera.hpp"
#include "Environment.hpp"
#include "FrameGraphHelper.hpp"
#include "GeometryManager.hpp"
#include "Input.hpp"
#include "LightManager.hpp"
#include "Material.hpp"
#include "MaterialManager.hpp"
#include "ModelLoader.hpp"
#include "RenderDevice.hpp"
#include "RenderPassCommon.hpp"
#include "TextureLoader.hpp"
#include "Util/DataUtil.hpp"
#include "Util/HelperUtils.hpp"
#include "Util/PerfCounter.hpp"
#include "Util/Profiling.hpp"
#include "fg/FrameGraph.hpp"
#include "fg/JsonWriter.hpp"

#include "imgui.h"
#include "imgui_impl_dx12.h"
#include "imgui_impl_win32.h"

namespace
{
std::unordered_map<SIZE_T, Ember::RawDescriptorHandle> g_ImguiHandleMap;

struct DebugConfig
{
  enum VisMode : uint32_t
  {
    kRender        = 0,
    kMeshlet       = 1,
    kWorldPosition = 2,
    kAlbedo        = 3,
    kNormal        = 4,
    kORM           = 5,
    kEmissive      = 6,
    kLightingOnly  = 7,
  };

  enum SkyMode : uint32_t
  {
    kNone       = 0,
    kSkybox     = 1,
    kAtmosphere = 2,
  };

  uint32_t ShowDebugUI                  = true;
  uint32_t ShowWireframe                = false;
  VisMode  VisualizationMode            = kRender;

  SkyMode  SkyMode                      = kAtmosphere;
  uint32_t RemoveDiffuseContrib         = false;
  uint32_t RemoveSpecularContrib        = false;

  uint32_t DisableMeshletFrustumCulling = false;
} g_Debug;

bool                  g_OutputFrameGraph        = false;
bool                  g_UseDeferredRendering    = false;

constexpr char const* kVisualizationModeNames[] = {
  "Render", "Meshlet", "World Position", "Albedo", "Normal", "ORM", "Emissive", "Lighting Only",
};
constexpr char const* kSkyModeNames[] = { "None", "Skybox", "Atmosphere" };

} // namespace

struct RotatingModel
{
  float Speed;
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
  , m_FGContext{}
  , m_RenderTargetManager{ std::move( render_target_manager ) }
  , m_SwapchainFormat{ m_RenderDevice->FetchSwapchainFormat() }
  , m_Camera{ std::make_unique<Camera>() }
  , m_Environment{ std::make_unique<Environment>() }
  , m_MaterialManager{ std::make_unique_for_overwrite<MaterialManager>() }
  , m_GeometryManager{ std::make_unique_for_overwrite<GeometryManager>() }
  , m_AtmosphereContext{ std::make_unique_for_overwrite<AtmosphereContext>() }
  , m_DrawList{ m_RenderDevice.get(), m_GeometryManager.get(), RenderDevice::kNumFrames }
  , m_LightManager{ std::make_unique_for_overwrite<LightManager>() }
{
  m_TextureLoader = std::make_unique_for_overwrite<TextureLoader>();
  TextureLoader::Create( m_TextureLoader.get(), m_RenderDevice.get(), 3 );

  ImGui_ImplWin32_EnableDpiAwareness();
  float main_scale =
      ImGui_ImplWin32_GetDpiScaleForMonitor( ::MonitorFromPoint( POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY ) );
  // Setup Dear ImGui context
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  ( void )io;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;  // Enable Gamepad Controls
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  io.ConfigFlags |= ImGuiConfigFlags_DpiEnableScaleFonts;

  // Setup Dear ImGui style
  ImGui::StyleColorsDark();
  ImGuiStyle& style = ImGui::GetStyle();
  style.ScaleAllSizes( main_scale ); // Bake a fixed style scale. (until we have a solution for dynamic style scaling,
                                     // changing this requires resetting Style + calling this again)

  // Setup Platform/Renderer backends
  ImGui_ImplWin32_Init( window_handle );

  ImGui_ImplDX12_InitInfo init_info = {};
  init_info.Device                  = m_RenderDevice->GetDevice();
  init_info.CommandQueue            = m_RenderDevice->GetDirectQueue();
  init_info.NumFramesInFlight       = RenderDevice::kNumFrames;
  init_info.RTVFormat               = DXGI_FORMAT_R8G8B8A8_UNORM;
  init_info.DSVFormat               = DXGI_FORMAT_UNKNOWN;
  // Allocating SRV descriptors (for textures) is up to the application, so we provide callbacks.
  // (current version of the backend will only allocate one descriptor, future versions will need to allocate more)
  init_info.SrvDescriptorHeap    = m_RenderDevice->GetBindlessDescriptorHeaps()[0];
  init_info.SrvDescriptorAllocFn = []( ImGui_ImplDX12_InitInfo* init_info,
                                       D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu_handle,
                                       D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu_handle )
  {
    RenderDevice const* render_device = ( RenderDevice* )init_info->UserData;
    g_ImguiHandleMap[out_cpu_handle->ptr] =
        render_device->AllocateRawDescriptorHandle( out_cpu_handle, out_gpu_handle );
  };
  init_info.SrvDescriptorFreeFn =
      []( ImGui_ImplDX12_InitInfo* init_info, D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle, D3D12_GPU_DESCRIPTOR_HANDLE )
  {
    RenderDevice const* render_device = ( RenderDevice* )init_info->UserData;
    render_device->FreeHandle( g_ImguiHandleMap[cpu_handle.ptr] );
    g_ImguiHandleMap.erase( cpu_handle.ptr );
  };
  init_info.UserData = m_RenderDevice.get();
  ImGui_ImplDX12_Init( &init_info );

  m_FGBlackboard.add<PerFrameConstants>();
  m_FGBlackboard.add<DrawList::Batches>();
  m_FGBlackboard.add<Environment::GpuRepr>();
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
  PerfCounter::Create( perf_counter.get(), render_device->GetDevice(), RenderDevice::kNumFrames );

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

void Ember::BasicApp::SetupRenderPasses()
{
  ENSURE( RenderPass::OpaqueForward::Create(
      &m_OpaquePass, m_RenderDevice.get(), DirectX::MakeSRGB( m_SwapchainFormat ) ) );

  ENSURE( RenderPass::GBuffer::Create( &m_GBufferPass, m_RenderDevice.get() ) );
  ENSURE( RenderPass::OmniLightDeferred::Create(
      &m_OmniLightPass, m_RenderDevice.get(), DirectX::MakeSRGB( m_SwapchainFormat ) ) );
  ENSURE( RenderPass::SpotLightDeferred::Create(
      &m_SpotLightPass, m_RenderDevice.get(), DirectX::MakeSRGB( m_SwapchainFormat ) ) );
  ENSURE( RenderPass::ScreenSpaceLightDeferred::Create(
      &m_ScreenSpaceLightPass, m_RenderDevice.get(), DirectX::MakeSRGB( m_SwapchainFormat ) ) );

  ENSURE( RenderPass::AlphaTestedForward::Create(
      &m_AlphaTestedPass, m_RenderDevice.get(), DirectX::MakeSRGB( m_SwapchainFormat ) ) );
  ENSURE( RenderPass::TransparencyForward::Create(
      &m_TransparencyPass, m_RenderDevice.get(), DirectX::MakeSRGB( m_SwapchainFormat ) ) );
  ENSURE( RenderPass::Background::Create(
      &m_BackgroundPass, m_RenderDevice.get(), DirectX::MakeSRGB( m_SwapchainFormat ) ) );
}

void Ember::BasicApp::LoadContent()
{
  ERR_ABORT( ::ShowWindow( m_WindowHandle, SW_SHOW ) );

  // Setup Debug
  m_ConfigurationBuffer = m_RenderDevice->CreateConstantBuffer( sizeof( DebugConfig ) );

  // Setup Camera
  Camera::Create( m_Camera.get(), m_RenderDevice.get(), RenderDevice::kNumFrames );

  m_Camera->SetHorizontalFoV( DirectX::XMConvertToRadians( 70.0f ) );
  m_Camera->SetAspectRatio( ( float )m_WindowWidth / ( float )m_WindowHeight );
  m_Camera->SetYawPitch( DirectX::XM_PI * 5.0f / 4.0f, 0.0f );
  m_Camera->SetPosition( DirectX::XMVectorSet( -23.0f, 2.0f, -10.0f, 1.0f ) );

  // Setup Lights
  LightManager::Create( m_LightManager.get(), m_RenderDevice.get(), &m_World, RenderDevice::kNumFrames );
  m_LightManager->AddShadowingDirLight( { 1.0f, -1.0f, 0.0f }, Color32::White(), 5.0f );

  m_World.GetECS()
      .entity( "OmniLight 0" )
      .add<ShadowCaster>()
      .insert(
          [&]( WorldTransform&, LocalTransform& lt, OmniLight& ol )
          {
            lt.Translation = { 15.0f, 2.0f, 12.0f };
            ol.Color       = Color32::Blue();
            ol.Intensity   = 15.0f;
            ol.Range       = 10.0f;
          } );

  m_World.GetECS()
      .entity( "OmniLight 1" )
      .add<ShadowCaster>()
      .insert(
          [&]( WorldTransform&, LocalTransform& lt, OmniLight& ol )
          {
            lt.Translation = { 0.0f, 2.0f, 5.0f };
            ol.Color       = Color32::Green();
            ol.Intensity   = 15.0f;
            ol.Range       = 10.0f;
          } );

  m_World.GetECS()
      .entity( "OmniLight 2" )
      .add<ShadowCaster>()
      .insert(
          [&]( WorldTransform&, LocalTransform& lt, OmniLight& ol )
          {
            lt.Translation = { -15.0f, 2.0f, -5.0f };
            ol.Color       = Color32::Red();
            ol.Intensity   = 25.0f;
            ol.Range       = 10.0f;
          } );

  m_World.GetECS()
      .entity( "SpotLight" )
      .add<ShadowCaster>()
      .insert(
          [&]( WorldTransform&, LocalTransform& lt, SpotLight& sl, RotatingModel& rm )
          {
            lt.Translation = { -15.0f, 1.0f, -5.0f };
            lt.Rotation    = DirectX::XMQuaternionRotationRollPitchYaw(
                DirectX::XMConvertToRadians( -10.0f ), DirectX::XMConvertToRadians( 0.0f ), 0.0f );
            sl.Color              = Color32::White();
            sl.ConeInnerHalfAngle = DirectX::XMConvertToRadians( 10.0f );
            sl.ConeOuterHalfAngle = DirectX::XMConvertToRadians( 15.0f );
            sl.Intensity          = 50.0f;
            sl.Range              = 20.0f;
            rm.Speed              = 20.0f;
          } );

  MaterialManager::Create( m_MaterialManager.get(), m_RenderDevice.get(), 10'000 );
  GeometryManager::Create( m_GeometryManager.get(), m_RenderDevice.get(), 256_MiB );

  m_ModelLoader = std::make_unique<ModelLoader>(
      m_RenderDevice.get(), &m_World, m_TextureLoader.get(), m_MaterialManager.get(), m_GeometryManager.get() );

  // Setup Scene Geometry
  flecs::entity       model = m_ModelLoader->TryLoadModel( "Sponza.glb" ).value().set_name( "Scene" );

  flecs::entity const rm =
      m_World.GetECS()
          .entity( "HelmetRotator" )
          .insert(
              []( LocalTransform& local_tx, WorldTransform&, RotatingModel& rot_model, WorldBoundingBox& )
              {
                rot_model.Speed      = 20.0f;
                local_tx.Translation = DirectX::XMVectorSet( 0.0f, 1.0f, 5.0f, 1.0f );
              } );

  model                             = m_ModelLoader->TryLoadModel( "DamagedHelmet.glb" )->child_of( rm );
  LocalTransform* local_tx          = model.get_mut<LocalTransform>();
  local_tx->Scale                   = DirectX::XMVectorSet( 0.3f, 0.3f, 0.3f, 0.0f );
  local_tx->Rotation                = DirectX::XMQuaternionIdentity();

  model                             = m_ModelLoader->TryLoadModel( "AlphaBlendModeTest.glb" ).value();
  local_tx                          = model.get_mut<LocalTransform>();
  local_tx->Translation             = DirectX::XMVectorSet( 5.0f, 2.0f, 7.0f, 1.0f );
  local_tx->Rotation                = DirectX::XMQuaternionIdentity();

  constexpr char const* kEnvMapFile = "OvercastSoil.hdr";
  bool const            env_loaded =
      Environment::TryLoadFrom( m_Environment.get(), m_RenderDevice.get(), m_TextureLoader.get(), kEnvMapFile );
  ENSURE( env_loaded );

  SetupRenderPasses();
  FG::Context::Create( &m_FGContext, m_RenderDevice.get() );

  ENSURE( AtmosphereContext::Create( m_AtmosphereContext.get(), m_RenderDevice.get() ) );

  m_PrevMouse   = Input::Instance().GetMousePosition();

  m_RenderQuery = m_World.GetECS().query<WorldTransform const, Mesh const, Geometry const, Material const>();
}

void Ember::BasicApp::Update()
{
  ZoneScoped;

  m_PerfCounter->Tick();

  double const      avg_delta_ms   = m_PerfCounter->GetAvgFrameTime();
  double const      avg_fps        = 1000.0f / avg_delta_ms;

  auto&             pipeline_stats = m_PerfCounter->GetPipelineStats();

  DirectX::XMFLOAT3 cam_pos;
  XMStoreFloat3( &cam_pos, m_Camera->GetPosition() );

  float const cam_pitch = m_Camera->GetPitch();
  float const cam_yaw   = m_Camera->GetYaw();

  ImGui_ImplDX12_NewFrame();
  ImGui_ImplWin32_NewFrame();
  ImGui::NewFrame();

  if ( g_Debug.ShowDebugUI )
  {
    {
      ImGui::Begin( "Ember Info" );

      ImGui::Text( "Resolution: %ux%u", m_WindowWidth, m_WindowHeight );
      ImGui::Text( "Frame Time %.3lf ms (%.2lf FPS)", avg_delta_ms, avg_fps );
      ImGui::PlotLines(
          "FrameTime",
          m_PerfCounter->GetDeltaValues(),
          PerfCounter::kSampleCount,
          0,
          nullptr,
          0,
          PerfCounter::kMaxDeltaMs );

      ImGui::Text( "Transient Textures: %u", m_FGContext.GetTextureCount() );

      if ( ImGui::CollapsingHeader( "MeshDraws" ) )
      {
        ImGui::Text( "Total: %llu", m_DrawList.GetTotalCount() );
        ImGui::Text( "Opaque: %llu", m_DrawList.GetOpaqueCount() );
        ImGui::Text( "Alpha Tested: %llu", m_DrawList.GetAlphaTestedCount() );
        ImGui::Text( "Alpha Blended: %llu", m_DrawList.GetAlphaBlendedCount() );
      }

      if ( ImGui::CollapsingHeader( "Pipeline Stats" ) )
      {
        if ( ImGui::Button( "Gather Invocation Info" ) )
        {
          m_PerfCounter->GatherPipelineStatistics();
        }
        ImGui::Text( "IA Vertices: %llu", pipeline_stats.IAVertices );
        ImGui::Text( "IA Primitives: %llu", pipeline_stats.IAPrimitives );
        ImGui::Text( "VS Invocation: %llu", pipeline_stats.VSInvocations );
        ImGui::Text( "GS Invocation: %llu", pipeline_stats.GSInvocations );
        ImGui::Text( "C Invocation: %llu", pipeline_stats.CInvocations );
        ImGui::Text( "C Primitives: %llu", pipeline_stats.CPrimitives );
        ImGui::Text( "PS Invocation: %llu", pipeline_stats.PSInvocations );
        ImGui::Text( "HS Invocation: %llu", pipeline_stats.HSInvocations );
        ImGui::Text( "DS Invocation: %llu", pipeline_stats.DSInvocations );
        ImGui::Text( "CS Invocation: %llu", pipeline_stats.CSInvocations );
        ImGui::Text( "AS Invocation: %llu", pipeline_stats.ASInvocations );
        ImGui::Text( "MS Invocation: %llu", pipeline_stats.MSInvocations );
        ImGui::Text( "MS Primitives: %llu", pipeline_stats.MSPrimitives );
      }
      ImGui::End();
    }

    {
      ImGui::Begin( "Debug" );
      {
        bool scratch;
        scratch = ( bool )g_Debug.ShowWireframe;
        ImGui::Checkbox( "Show Wireframe", &scratch );
        g_Debug.ShowWireframe = ( uint32_t )scratch;

        ImGui::Combo(
            "Show Light Only",
            ( int* )&g_Debug.VisualizationMode,
            DataOf( kVisualizationModeNames ),
            CountOf( kVisualizationModeNames ),
            5 );

        ImGui::Combo( "Skybox", ( int* )&g_Debug.SkyMode, DataOf( kSkyModeNames ), CountOf( kSkyModeNames ), 5 );

        scratch = ( bool )g_Debug.RemoveDiffuseContrib;
        ImGui::Checkbox( "Remove Diffuse Contribution", &scratch );
        g_Debug.RemoveDiffuseContrib = ( uint32_t )scratch;

        scratch                      = ( bool )g_Debug.RemoveSpecularContrib;
        ImGui::Checkbox( "Remove Specular Contribution", &scratch );
        g_Debug.RemoveSpecularContrib = ( uint32_t )scratch;

        scratch                       = ( bool )g_Debug.DisableMeshletFrustumCulling;
        ImGui::Checkbox( "Disable Meshlet Frustum Culling", &scratch );
        g_Debug.DisableMeshletFrustumCulling = ( uint32_t )scratch;

        ImGui::Checkbox( "Use Deferred Rendering", &g_UseDeferredRendering );
        g_OutputFrameGraph = ImGui::Button( "Output FrameGraph" );
      }
      ImGui::End();
    }

    {
      ImGui::Begin( "Camera" );

      float gi_cam_yaw_pitch[] = { cam_yaw, cam_pitch };
      if ( ImGui::DragFloat2( "Yaw & Pitch", gi_cam_yaw_pitch, 0.01f ) )
      {
        m_Camera->SetYawPitch( gi_cam_yaw_pitch[0], gi_cam_yaw_pitch[1] );
      }
      float gi_cam_pos[] = { cam_pos.x, cam_pos.y, cam_pos.z };
      if ( ImGui::DragFloat3( "Position", gi_cam_pos, 0.1f ) )
      {
        m_Camera->SetPosition( gi_cam_pos[0], gi_cam_pos[1], gi_cam_pos[2] );
      }

      ImGui::Text( "Mouse Position: %u %u", m_PrevMouse.x, m_PrevMouse.y );

      ImGui::End();
    }
  }

  m_ConfigurationBuffer.Write( 0, sizeof( g_Debug ), &g_Debug );

  // Rendering
  ImGui::Render();

  float const delta_seconds = m_PerfCounter->GetDeltaMilliSeconds() * 0.001f;

  m_TextureLoader->Update();

  DirectX::XMUINT2 const mouse_pos = Input::Instance().GetMousePosition();
  float const            mouse_dx  = ( ( float )mouse_pos.x - ( float )m_PrevMouse.x ) / ( float )m_WindowWidth;
  float const            mouse_dy  = ( ( float )mouse_pos.y - ( float )m_PrevMouse.y ) / ( float )m_WindowHeight;
  m_PrevMouse                      = mouse_pos;

  if ( Input::Instance().IsRightMouseDown() )
    m_Camera->SetYawPitch(
        m_Camera->GetYaw() - DirectX::XM_PI * mouse_dx, m_Camera->GetPitch() - DirectX::XM_PIDIV2 * mouse_dy );

  if ( Input::Instance().IsPressed( 'R' ) or Input::Instance().IsPressed( 'A' ) )
  {
    m_Camera->LocalTranslate( -5 * delta_seconds, 0, 0 );
  }
  if ( Input::Instance().IsPressed( 'F' ) or Input::Instance().IsPressed( 'W' ) )
  {
    m_Camera->LocalTranslate( 0, 0, -5 * delta_seconds );
  }
  if ( Input::Instance().IsPressed( 'S' ) )
  {
    m_Camera->LocalTranslate( 0, 0, 5 * delta_seconds );
  }
  if ( Input::Instance().IsPressed( 'T' ) or Input::Instance().IsPressed( 'D' ) )
  {
    m_Camera->LocalTranslate( 5 * delta_seconds, 0, 0 );
  }
  if ( Input::Instance().IsPressed( 'Z' ) )
  {
    m_Camera->LocalTranslate( 0, -5 * delta_seconds, 0 );
  }
  if ( Input::Instance().IsPressed( 'X' ) )
  {
    m_Camera->LocalTranslate( 0, 5 * delta_seconds, 0 );
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

  Input::Instance().Update();
}

Ember::RenderPass::RTVData Ember::BasicApp::ClearRenderTargets( FrameGraph* frame_graph ) const
{
  return frame_graph->addCallbackPass(
      "Clear RTV",
      [&]( FrameGraph::Builder& builder, RenderPass::RTVData& data )
      {
        data.RenderTarget = builder.create<FG::Texture>(
            "Main Render Target",
            FG::Texture::Desc{
                .Format    = m_SwapchainFormat,
                .Width     = m_WindowWidth,
                .Height    = m_WindowHeight,
                .MipLevels = MipLevels::kBase,
                .InitState = D3D12_RESOURCE_STATE_RENDER_TARGET,
                .Flags     = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
            } );

        data.DepthStencil = builder.create<FG::Texture>(
            "Main Depth Target",
            FG::Texture::Desc{
                .Format    = DXGI_FORMAT_D32_FLOAT,
                .Width     = m_WindowWidth,
                .Height    = m_WindowHeight,
                .MipLevels = MipLevels::kBase,
                .InitState = D3D12_RESOURCE_STATE_DEPTH_WRITE,
                .Flags     = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL,
            } );

        data.RenderTarget = builder.write( data.RenderTarget, FG::Attachment{ .Index = 0, .IsSrgb = true } );
        data.DepthStencil = builder.write( data.DepthStencil, FG::DepthStencil{} );
      },
      []( RenderPass::RTVData const& data, FrameGraphPassResources& resources, FG::Context const* context )
      {
        FG::Context::FrameData const& frame_data    = context->GetFrameData();
        RenderTargetManager const*    rtm           = context->GetRenderTargetManager();

        FG::Texture const&            render_target = resources.get<FG::Texture>( data.RenderTarget );
        FG::Texture const&            depth_target  = resources.get<FG::Texture>( data.DepthStencil );

        FLOAT constexpr kBlack[4]                   = {};
        rtm->ClearRenderTargetView( frame_data.CommandList, render_target.Resource.Get(), kBlack );
        rtm->ClearDepthStencilView(
            frame_data.CommandList, depth_target.Resource.Get(), D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0 );
      } );
}

Ember::RenderPass::RTVData Ember::BasicApp::RenderTransparency(
    FrameGraph* frame_graph, RenderPass::RTVData const& opaque_pass )
{
  RenderPass::RTVData alpha_tested_pass = frame_graph->addCallbackPass(
      "Alpha Tested Pass",
      [&]( FrameGraph::Builder& builder, RenderPass::RTVData& data )
      {
        data.RenderTarget = builder.write( opaque_pass.RenderTarget, FG::Attachment{ .Index = 0, .IsSrgb = true } );
        data.DepthStencil = builder.write( opaque_pass.DepthStencil, FG::DepthStencil{} );
      },
      [mp = m_AlphaTestedPass,
       bb = &m_FGBlackboard]( RenderPass::RTVData const&, FrameGraphPassResources&, FG::Context const* context )
      {
        ZoneScopedN( "Alpha Tested Pass" );

        FG::Context::FrameData const& frame_data = context->GetFrameData();
        ID3D12GraphicsCommandList6*   cmd        = frame_data.CommandList;
        PIXScopedEvent( cmd, PIX_COLOR_DEFAULT, "Alpha Tested Pass" );

        DrawList::Batches const&    draw_list_info_list = bb->get<DrawList::Batches>();
        PerFrameConstants const&    constants           = bb->get<PerFrameConstants>();
        Environment::GpuRepr const& env                 = bb->get<Environment::GpuRepr>();

        cmd->SetGraphicsRootSignature( mp.RootSignature.Get() );
        // TODO: Sort transparent objects back to front
        cmd->SetPipelineState( mp.Pipeline.Get() );
        cmd->SetGraphicsRoot32BitConstants( 0, sizeof( DrawList::Info ) / 4, &draw_list_info_list.AlphaTested, 0 );
        cmd->SetGraphicsRoot32BitConstants( 1, sizeof( PerFrameConstants ) / 4, &constants, 0 );
        cmd->SetGraphicsRoot32BitConstants( 2, sizeof( Environment::GpuRepr ) / 4, &env, 0 );
        cmd->DispatchMesh( draw_list_info_list.AlphaTested.DrawCount, 1, 1 );
      } );

  return frame_graph->addCallbackPass(
      "Transparency Pass",
      [&]( FrameGraph::Builder& builder, RenderPass::RTVData& data )
      {
        data.RenderTarget =
            builder.write( alpha_tested_pass.RenderTarget, FG::Attachment{ .Index = 0, .IsSrgb = true } );
        data.DepthStencil = builder.write( alpha_tested_pass.DepthStencil, FG::DepthStencil{} );
      },
      [mp = m_TransparencyPass,
       bb = &m_FGBlackboard]( RenderPass::RTVData const&, FrameGraphPassResources&, FG::Context* context )
      {
        ZoneScopedN( "Transparency Pass" );

        FG::Context::FrameData const& frame_data = context->GetFrameData();
        ID3D12GraphicsCommandList6*   cmd        = frame_data.CommandList;
        PIXScopedEvent( cmd, PIX_COLOR_DEFAULT, "Transparency Pass" );

        DrawList::Batches const&    draw_list_info_list = bb->get<DrawList::Batches>();
        PerFrameConstants const&    constants           = bb->get<PerFrameConstants>();
        Environment::GpuRepr const& env                 = bb->get<Environment::GpuRepr>();

        cmd->SetGraphicsRootSignature( mp.RootSignature.Get() );
        // TODO: Sort transparent objects back to front
        cmd->SetPipelineState( mp.Pipeline.Get() );
        cmd->SetGraphicsRoot32BitConstants( 0, sizeof( DrawList::Info ) / 4, &draw_list_info_list.AlphaBlended, 0 );
        cmd->SetGraphicsRoot32BitConstants( 1, sizeof( PerFrameConstants ) / 4, &constants, 0 );
        cmd->SetGraphicsRoot32BitConstants( 2, sizeof( Environment::GpuRepr ) / 4, &env, 0 );
        cmd->DispatchMesh( draw_list_info_list.AlphaBlended.DrawCount, 1, 1 );
      } );
}

Ember::RenderPass::RTVData Ember::BasicApp::RenderOpaqueFwd(
    FrameGraph* frame_graph, RenderPass::RTVData const& clear_rtv )
{
  return frame_graph->addCallbackPass(
      "Opaque Forward",
      [&]( FrameGraph::Builder& builder, RenderPass::RTVData& data )
      {
        data.RenderTarget = builder.write( clear_rtv.RenderTarget, FG::Attachment{ .Index = 0, .IsSrgb = true } );
        data.DepthStencil = builder.write( clear_rtv.DepthStencil, FG::DepthStencil{} );
      },
      [mp = m_OpaquePass,
       bb = &m_FGBlackboard]( RenderPass::RTVData const&, FrameGraphPassResources&, FG::Context* context )
      {
        ZoneScopedN( "Opaque Forward" );

        FG::Context::FrameData const& frame_data = context->GetFrameData();
        ID3D12GraphicsCommandList6*   cmd        = frame_data.CommandList;
        PIXScopedEvent( cmd, PIX_COLOR_DEFAULT, "Opaque Forward" );

        DrawList::Batches const&    draw_list_info_list = bb->get<DrawList::Batches>();
        PerFrameConstants const&    constants           = bb->get<PerFrameConstants>();
        Environment::GpuRepr const& env                 = bb->get<Environment::GpuRepr>();

        cmd->SetGraphicsRootSignature( mp.RootSignature.Get() );
        cmd->SetGraphicsRoot32BitConstants( 1, sizeof( PerFrameConstants ) / 4, &constants, 0 );
        cmd->SetGraphicsRoot32BitConstants( 2, sizeof( Environment::GpuRepr ) / 4, &env, 0 );

        cmd->SetPipelineState( mp.Pipeline.Get() );
        cmd->SetGraphicsRoot32BitConstants( 0, sizeof( DrawList::Info ) / 4, &draw_list_info_list.Opaque, 0 );
        cmd->DispatchMesh( draw_list_info_list.Opaque.DrawCount, 1, 1 );
      } );
}

Ember::RenderPass::RTVData Ember::BasicApp::RenderSkybox(
    FrameGraph*                       frame_graph,
    RenderPass::RTVData const&        transparency_pass,
    AtmosphereContext::OutData const& atmosphere )
{
  struct SkyboxData
  {
    FrameGraphResource RenderTarget;
    FrameGraphResource DepthStencil;
    FrameGraphResource SkyViewLUT;
    bool               UseProcAtmos{ false };
  };

  SkyboxData skybox = frame_graph->addCallbackPass(
      "Render Skybox",
      [&]( FrameGraph::Builder& builder, SkyboxData& data )
      {
        if ( g_Debug.SkyMode == DebugConfig::kNone )
        {
          data.RenderTarget = transparency_pass.RenderTarget;
          data.DepthStencil = transparency_pass.DepthStencil;
          return;
        }

        data.RenderTarget =
            builder.write( transparency_pass.RenderTarget, FG::Attachment{ .Index = 0, .IsSrgb = true } );
        data.DepthStencil = builder.write( transparency_pass.DepthStencil, FG::DepthStencil{} );

        if ( g_Debug.SkyMode == DebugConfig::kAtmosphere )
        {
          data.SkyViewLUT = builder.read(
              atmosphere.SkyViewLUT,
              FG::ShaderResource{
                  .PixelShaderUse = true,
                  .OnlyTopMip     = true,
              } );
          data.UseProcAtmos = true;
        }
      },
      [mbp = m_BackgroundPass,
       bb  = &m_FGBlackboard]( SkyboxData const& data, FrameGraphPassResources& resources, FG::Context const* context )
      {
        ZoneScopedN( "Render Skybox" );

        FG::Context::FrameData const& frame_data = context->GetFrameData();
        ID3D12GraphicsCommandList6*   cmd        = frame_data.CommandList;

        PIXScopedEvent( cmd, PIX_COLOR_DEFAULT, "Render Skybox" );

        PerFrameConstants const&    constants = bb->get<PerFrameConstants>();
        Environment::GpuRepr const& env       = bb->get<Environment::GpuRepr>();

        cmd->SetGraphicsRootSignature( mbp.RootSignature.Get() );
        cmd->SetGraphicsRoot32BitConstant( 0, ( UINT )constants.Camera, 0 );

        if ( data.UseProcAtmos )
        {
          FG::Texture const& sky_view = resources.get<FG::Texture>( data.SkyViewLUT );
          cmd->SetPipelineState( mbp.AtmospherePipeline.Get() );
          cmd->SetGraphicsRoot32BitConstant( 0, ( UINT )sky_view.AsSRV, 1 );
        }
        else
        {
          cmd->SetPipelineState( mbp.SkyboxPipeline.Get() );
          cmd->SetGraphicsRoot32BitConstant( 0, ( UINT )env.Skybox, 1 );
        }

        cmd->DrawInstanced( 3, 1, 0, 0 );
      } );

  return { skybox.RenderTarget, skybox.DepthStencil };
}

Ember::RenderPass::RTVData Ember::BasicApp::RenderOpaqueDfr(
    FrameGraph* frame_graph, RenderPass::RTVData const& clear_rtv )
{
  RenderPass::GBufferData clear_gbuffer = frame_graph->addCallbackPass(
      "Clear GBuffer",
      [&]( FrameGraph::Builder& builder, RenderPass::GBufferData& data )
      {
        FG::Texture::Desc desc{
          .Format    = RenderPass::GBuffer::kGBufferFormats[RenderPass::GBuffer::kPosition],
          .Width     = m_WindowWidth,
          .Height    = m_WindowHeight,
          .MipLevels = MipLevels::kBase,
          .InitState = D3D12_RESOURCE_STATE_RENDER_TARGET,
          .Flags     = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
        };

        for ( int i = 0; i < RenderPass::GBuffer::kGBufferCount; i++ )
        {
          desc.Format     = RenderPass::GBuffer::kGBufferFormats[i];
          data.GBuffer[i] = builder.create<FG::Texture>( RenderPass::GBuffer::kGBufferNames[i], desc );
          data.GBuffer[i] = builder.write( data.GBuffer[i], FG::Attachment{ .Index = ( uint8_t )i } );
        }
      },
      []( RenderPass::GBufferData const& data, FrameGraphPassResources& resources, FG::Context const* context )
      {
        FG::Context::FrameData const& frame_data = context->GetFrameData();
        RenderTargetManager const*    rtm        = context->GetRenderTargetManager();

        ID3D12Resource*               gbuffer[RenderPass::GBuffer::kGBufferCount];

        for ( int i = 0; i < RenderPass::GBuffer::kGBufferCount; i++ )
        {
          gbuffer[i] = resources.get<FG::Texture>( data.GBuffer[i] ).Resource.Get();
        }

        FLOAT constexpr kBlack[4] = {};
        rtm->ClearRenderTargetViews( frame_data.CommandList, CountOf( gbuffer ), DataOf( gbuffer ), kBlack );
      } );

  RenderPass::GBufferData gbuffer = frame_graph->addCallbackPass(
      "GBuffer Pass",
      [&]( FrameGraph::Builder& builder, RenderPass::GBufferData& data )
      {
        for ( uint32_t i = 0; i < RenderPass::GBuffer::kGBufferCount; i++ )
        {
          data.GBuffer[i] = builder.write( clear_gbuffer.GBuffer[i], FG::Attachment{ .Index = ( uint8_t )i } );
        }
        data.DepthStencil = builder.write( clear_rtv.DepthStencil, FG::DepthStencil{} );
      },
      [mp = m_GBufferPass,
       bb = &m_FGBlackboard]( RenderPass::GBufferData const&, FrameGraphPassResources&, FG::Context const* context )
      {
        ZoneScopedN( "GBuffer Pass" );

        FG::Context::FrameData const& frame_data = context->GetFrameData();
        ID3D12GraphicsCommandList6*   cmd        = frame_data.CommandList;
        PIXScopedEvent( cmd, PIX_COLOR_DEFAULT, "GBuffer Pass" );

        DrawList::Batches const&    draw_list_info_list = bb->get<DrawList::Batches>();
        PerFrameConstants const&    constants           = bb->get<PerFrameConstants>();
        Environment::GpuRepr const& env                 = bb->get<Environment::GpuRepr>();

        cmd->SetGraphicsRootSignature( mp.RootSignature.Get() );
        cmd->SetPipelineState( mp.Pipeline.Get() );
        cmd->SetGraphicsRoot32BitConstants( 0, sizeof( DrawList::Info ) / 4, &draw_list_info_list.Opaque, 0 );
        cmd->SetGraphicsRoot32BitConstants( 1, sizeof( PerFrameConstants ) / 4, &constants, 0 );
        cmd->SetGraphicsRoot32BitConstants( 2, sizeof( Environment::GpuRepr ) / 4, &env, 0 );
        cmd->DispatchMesh( draw_list_info_list.Opaque.DrawCount, 1, 1 );
      } );

  RenderPass::MergeData omni_pass = frame_graph->addCallbackPass(
      "OmniLight Pass",
      [&]( FrameGraph::Builder& builder, RenderPass::MergeData& data )
      {
        for ( uint32_t i = 0; i < RenderPass::GBuffer::kGBufferCount; i++ )
        {
          data.GBuffer[i] = builder.read( gbuffer.GBuffer[i], FG::ShaderResource{ .PixelShaderUse = true } );
        }
        data.RenderTarget = builder.write( clear_rtv.RenderTarget, FG::Attachment{ .Index = 0, .IsSrgb = true } );
        data.DepthStencil = builder.write( gbuffer.DepthStencil, FG::DepthStencil{} );
      },
      [mp = m_OmniLightPass, bb = &m_FGBlackboard](
          RenderPass::MergeData const& data, FrameGraphPassResources& resources, FG::Context const* context )
      {
        ZoneScopedN( "OmniLight Pass" );

        FG::Context::FrameData const& frame_data = context->GetFrameData();
        ID3D12GraphicsCommandList6*   cmd        = frame_data.CommandList;
        PIXScopedEvent( cmd, PIX_COLOR_DEFAULT, "OmniLight Pass" );

        PerFrameConstants const&    constants = bb->get<PerFrameConstants>();
        Environment::GpuRepr const& env       = bb->get<Environment::GpuRepr>();

        SRVHandle                   gbuffer_handles[RenderPass::GBuffer::kGBufferCount];
        for ( uint32_t i = 0; i < RenderPass::GBuffer::kGBufferCount; i++ )
        {
          gbuffer_handles[i] = resources.get<FG::Texture>( data.GBuffer[i] ).AsSRV;
        }

        cmd->SetGraphicsRootSignature( mp.RootSignature.Get() );
        cmd->SetPipelineState( mp.Pipeline.Get() );
        cmd->SetGraphicsRoot32BitConstants( 0, CountOf( gbuffer_handles ), DataOf( gbuffer_handles ), 0 );
        cmd->SetGraphicsRoot32BitConstants( 1, sizeof( PerFrameConstants ) / 4, &constants, 0 );
        cmd->SetGraphicsRoot32BitConstants( 2, sizeof( Environment::GpuRepr ) / 4, &env, 0 );
        cmd->DispatchMesh( ( constants.LightInfo.OmniLightInfo.TotalLightCount + 31 ) / 32, 1, 1 );
      } );

  RenderPass::MergeData const spot_pass = frame_graph->addCallbackPass(
      "SpotLight Pass",
      [&]( FrameGraph::Builder& builder, RenderPass::MergeData& data )
      {
        for ( uint32_t i = 0; i < RenderPass::GBuffer::kGBufferCount; i++ )
        {
          data.GBuffer[i] = builder.read( gbuffer.GBuffer[i], FG::ShaderResource{ .PixelShaderUse = true } );
        }
        data.RenderTarget = builder.write( omni_pass.RenderTarget, FG::Attachment{ .Index = 0, .IsSrgb = true } );
        data.DepthStencil = builder.write( omni_pass.DepthStencil, FG::DepthStencil{} );
      },
      [mp = m_SpotLightPass, bb = &m_FGBlackboard](
          RenderPass::MergeData const& data, FrameGraphPassResources& resources, FG::Context const* context )
      {
        ZoneScopedN( "SpotLight Pass" );

        FG::Context::FrameData const& frame_data = context->GetFrameData();
        ID3D12GraphicsCommandList6*   cmd        = frame_data.CommandList;
        PIXScopedEvent( cmd, PIX_COLOR_DEFAULT, "SpotLight Pass" );

        SRVHandle gbuffer_handles[RenderPass::GBuffer::kGBufferCount];
        for ( uint32_t i = 0; i < RenderPass::GBuffer::kGBufferCount; i++ )
        {
          gbuffer_handles[i] = resources.get<FG::Texture>( data.GBuffer[i] ).AsSRV;
        }

        PerFrameConstants const&    constants = bb->get<PerFrameConstants>();
        Environment::GpuRepr const& env       = bb->get<Environment::GpuRepr>();

        cmd->SetGraphicsRootSignature( mp.RootSignature.Get() );
        cmd->SetPipelineState( mp.Pipeline.Get() );
        cmd->SetGraphicsRoot32BitConstants( 0, CountOf( gbuffer_handles ), DataOf( gbuffer_handles ), 0 );
        cmd->SetGraphicsRoot32BitConstants( 1, sizeof( PerFrameConstants ) / 4, &constants, 0 );
        cmd->SetGraphicsRoot32BitConstants( 2, sizeof( Environment::GpuRepr ) / 4, &env, 0 );
        cmd->DispatchMesh( ( constants.LightInfo.SpotLightInfo.TotalLightCount + 31 ) / 32, 1, 1 );
      } );

  RenderPass::MergeData const screen_pass = frame_graph->addCallbackPass(
      "Screen Space Light Pass",
      [&]( FrameGraph::Builder& builder, RenderPass::MergeData& data )
      {
        for ( uint32_t i = 0; i < RenderPass::GBuffer::kGBufferCount; i++ )
        {
          data.GBuffer[i] = builder.read( gbuffer.GBuffer[i], FG::ShaderResource{ .PixelShaderUse = true } );
        }
        data.RenderTarget = builder.write( spot_pass.RenderTarget, FG::Attachment{ .Index = 0, .IsSrgb = true } );
        data.DepthStencil = builder.write( spot_pass.DepthStencil, FG::DepthStencil{} );
      },
      [mp = m_ScreenSpaceLightPass, bb = &m_FGBlackboard](
          RenderPass::MergeData const& data, FrameGraphPassResources& resources, FG::Context const* context )
      {
        ZoneScopedN( "Screen Space Light Pass" );

        FG::Context::FrameData const& frame_data = context->GetFrameData();
        ID3D12GraphicsCommandList6*   cmd        = frame_data.CommandList;
        PIXScopedEvent( cmd, PIX_COLOR_DEFAULT, "Screen Space Light Pass" );

        SRVHandle gbuffer_handles[RenderPass::GBuffer::kGBufferCount];
        for ( uint32_t i = 0; i < RenderPass::GBuffer::kGBufferCount; i++ )
        {
          gbuffer_handles[i] = resources.get<FG::Texture>( data.GBuffer[i] ).AsSRV;
        }

        PerFrameConstants const&    constants = bb->get<PerFrameConstants>();
        Environment::GpuRepr const& env       = bb->get<Environment::GpuRepr>();

        cmd->SetGraphicsRootSignature( mp.RootSignature.Get() );
        cmd->SetPipelineState( mp.Pipeline.Get() );
        cmd->SetGraphicsRoot32BitConstants( 0, CountOf( gbuffer_handles ), DataOf( gbuffer_handles ), 0 );
        cmd->SetGraphicsRoot32BitConstants( 1, sizeof( PerFrameConstants ) / 4, &constants, 0 );
        cmd->SetGraphicsRoot32BitConstants( 2, sizeof( Environment::GpuRepr ) / 4, &env, 0 );
        cmd->DispatchMesh( 1, 1, 1 );
      } );

  return { screen_pass.RenderTarget, screen_pass.DepthStencil };
}

void Ember::BasicApp::Render()
{
  ZoneScoped;

  m_FGContext.Update();

  ID3D12Resource*      backbuffer   = m_RenderDevice->GetCurrentBackbuffer();
  Context::CommandList command_list = m_RenderDevice->GetGraphicsCommandList();
  uint32_t const       frame_idx    = m_RenderDevice->GetCurrentFrameIndex();

  // All resources for this frame are guaranteed to be available for CPU modification at this time.

  m_FGContext.SetFrameData( {
      .CommandList = command_list.Get(),
      .Width       = m_WindowWidth,
      .Height      = m_WindowHeight,
  } );

  FrameGraph frame_graph;
  frame_graph.setPreExecCallback( []( FG::Context* context ) { context->PreparePass(); } );

  CBVHandle const camera_cbv = m_Camera->PrepareFrame( frame_idx );

  m_DrawList.Clear();

  {
    ZoneScopedN( "Upload Transforms" );
    m_RenderQuery.each( [&]( WorldTransform const& wt, Mesh const& mesh, Geometry const&, Material const& material )
                        { m_DrawList.PushDraw( wt, mesh, material ); } );
  }

  FrameGraphResource bb_res = frame_graph.import(
      "Backbuffer",
      FG::Texture::Desc{
          .Format    = m_SwapchainFormat,
          .Width     = m_WindowWidth,
          .Height    = m_WindowHeight,
          .MipLevels = MipLevels::kBase,
          .ArraySize = 1,
          .InitState = D3D12_RESOURCE_STATE_PRESENT,
      },
      FG::Texture{
          .Resource     = backbuffer,
          .CurrentState = D3D12_RESOURCE_STATE_PRESENT,
      } );

  DrawList::Batches const draw_list_info = m_DrawList.PrepareFrame( frame_idx );

  m_PerfCounter->UpdatePipelineStats( frame_idx );
  m_PerfCounter->BeginQuery( command_list.Get(), frame_idx );

  m_TextureLoader->FlushBarriers( command_list.Get() );

  auto bindless_desc_heaps = m_RenderDevice->GetBindlessDescriptorHeaps();
  command_list->SetDescriptorHeaps( CountOf( bindless_desc_heaps ), DataOf( bindless_desc_heaps ) );

  m_LightManager->RenderAllShadows( command_list.Get(), draw_list_info, *m_RenderTargetManager, *m_Camera, frame_idx );

  LightManager::GpuInfo light_info        = m_LightManager->PrepareFrame( frame_idx );
  SRVHandle const       materials_srv     = m_MaterialManager->PrepareFrame();

  m_FGBlackboard.get<PerFrameConstants>() = {
    .MaterialsBuffer = materials_srv,
    .Camera          = camera_cbv,
    .ConfigBuffer    = m_ConfigurationBuffer.GetCBVHandle(),
    .LightInfo       = light_info,
  };

  m_FGBlackboard.get<DrawList::Batches>()    = draw_list_info;
  m_FGBlackboard.get<Environment::GpuRepr>() = m_Environment->Repr();

  AtmosphereContext::OutData atmosphere      = m_AtmosphereContext->Render( &frame_graph, &m_FGBlackboard, frame_idx );

  RenderPass::RTVData        clear_rtv       = ClearRenderTargets( &frame_graph );
  RenderPass::RTVData        opaque_pass_fwd = RenderOpaqueFwd( &frame_graph, clear_rtv );
  RenderPass::RTVData        opaque_pass_dfr = RenderOpaqueDfr( &frame_graph, clear_rtv );

  RenderPass::RTVData        opaque_pass     = g_UseDeferredRendering ? opaque_pass_dfr : opaque_pass_fwd;

  RenderPass::RTVData        transparency_pass = RenderTransparency( &frame_graph, opaque_pass );
  RenderPass::RTVData        skybox_pass       = RenderSkybox( &frame_graph, transparency_pass, atmosphere );

  RenderPass::RTVData        rtv_data          = frame_graph.addCallbackPass(
      "ImGUI",
      [&]( FrameGraph::Builder& builder, RenderPass::RTVData& data )
      {
        data.RenderTarget = builder.write( skybox_pass.RenderTarget, FG::Attachment{ .Index = 0 } );
        data.DepthStencil = skybox_pass.DepthStencil;
      },
      []( RenderPass::RTVData const&, FrameGraphPassResources&, FG::Context const* context )
      {
        ZoneScopedN( "ImGUI" );

        FG::Context::FrameData const& frame_data = context->GetFrameData();
        ID3D12GraphicsCommandList6*   cmd        = frame_data.CommandList;

        PIXScopedEvent( cmd, PIX_COLOR_DEFAULT, "ImGUI" );

        ImGui_ImplDX12_RenderDrawData( ImGui::GetDrawData(), cmd );
      } );

  struct FinalPassData
  {
    FrameGraphResource RenderTarget;
    FrameGraphResource BackBuffer;
  };

  frame_graph.addCallbackPass(
      "Copy to Backbuffer",
      [&]( FrameGraph::Builder& builder, FinalPassData& data )
      {
        data.RenderTarget = builder.read( rtv_data.RenderTarget, FG::CopySrc{} );
        data.BackBuffer   = builder.write( bb_res, FG::CopyDst{} );
      },
      []( FinalPassData const& data, FrameGraphPassResources& resources, FG::Context* context )
      {
        ZoneScopedN( "Copy to Backbuffer" );

        FG::Context::FrameData const& frame_data = context->GetFrameData();
        ID3D12GraphicsCommandList6*   cmd        = frame_data.CommandList;

        PIXScopedEvent( cmd, PIX_COLOR_DEFAULT, "Copy to Backbuffer" );
        FG::Texture const& render_target = resources.get<FG::Texture>( data.RenderTarget );
        FG::Texture const& backbuffer    = resources.get<FG::Texture>( data.BackBuffer );
        cmd->CopyResource( backbuffer.Resource.Get(), render_target.Resource.Get() );
      } );

  frame_graph.compile();
  if ( g_OutputFrameGraph )
  {
    std::ofstream{ "fg.dot" } << frame_graph;
    std::ofstream f{ "fg.json" };
    frame_graph.debugOutput( f, JsonWriter{} );
  }
  frame_graph.execute( &m_FGContext, &m_FGContext );

  CD3DX12_RESOURCE_BARRIER bottom_of_renderpass_barriers[] = {
    CD3DX12_RESOURCE_BARRIER::Transition( backbuffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT ),
  };
  command_list->ResourceBarrier( CountOf( bottom_of_renderpass_barriers ), DataOf( bottom_of_renderpass_barriers ) );

  m_PerfCounter->EndQuery( command_list.Get(), frame_idx );

  m_RenderDevice->ExecuteCommandList( std::move( command_list ) );
  m_RenderDevice->Present();
}

void Ember::BasicApp::UnloadContent()
{
  m_RenderDevice->WaitIdle();

  ImGui_ImplDX12_Shutdown();
  ImGui_ImplWin32_Shutdown();
  ImGui::DestroyContext();
}

void Ember::BasicApp::Resize()
{
  RECT rect;
  ::GetClientRect( m_WindowHandle, &rect );

  m_WindowWidth  = rect.right - rect.left;
  m_WindowHeight = rect.bottom - rect.top;

  m_RenderDevice->ResizeSwapchain( m_WindowWidth, m_WindowHeight );

  m_Camera->SetAspectRatio( ( float )m_WindowWidth / ( float )m_WindowHeight );

  swprintf_s( m_SprintfBuffer, L"Ember %ux%u", m_WindowWidth, m_WindowHeight );
  SetWindowText( m_WindowHandle, m_SprintfBuffer );
}
