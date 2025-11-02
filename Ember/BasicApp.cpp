#include "BasicApp.hpp"

#include <cstdint>
#include <fstream>
#include <unordered_set>
#include <utility>

#include "Atmosphere.hpp"
#include "Camera.hpp"
#include "Environment.hpp"
#include "FrameGraphHelper.hpp"
#include "GeometryManager.hpp"
#include "Input.hpp"
#include "Inspector.hpp"
#include "LightManager.hpp"
#include "Material.hpp"
#include "MaterialManager.hpp"
#include "ModelLoader.hpp"
#include "PickingGizmo.hpp"
#include "RenderDevice.hpp"
#include "RenderPassCommon.hpp"
#include "SceneTree.hpp"
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

void Ember::BasicApp::InitImGui( HWND const window_handle, RenderDevice* render_device )
{
  ImGui_ImplWin32_EnableDpiAwareness();
  float main_scale =
      ImGui_ImplWin32_GetDpiScaleForMonitor( ::MonitorFromPoint( POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY ) );
  // Setup Dear ImGui context
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  ( void )io;
  // io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad; // Enable Gamepad Controls
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  io.ConfigFlags |= ImGuiConfigFlags_DpiEnableScaleFonts;

  // Setup Dear ImGui style
  ImGui::StyleColorsDark();
  ImGuiStyle& style = ImGui::GetStyle();
  style.ScaleAllSizes( main_scale ); // Bake a fixed style scale. (until we have a solution for dynamic style scaling,
                                     // changing this requires resetting Style + calling this again)

  // Setup Platform/Renderer backendss
  ImGui_ImplWin32_Init( window_handle );

  ImGui_ImplDX12_InitInfo init_info = {};
  init_info.Device                  = render_device->GetDevice();
  init_info.CommandQueue            = render_device->GetDirectQueue();
  init_info.NumFramesInFlight       = RenderDevice::kNumFrames;
  init_info.RTVFormat               = DXGI_FORMAT_R8G8B8A8_UNORM;
  init_info.DSVFormat               = DXGI_FORMAT_UNKNOWN;
  // Allocating SRV descriptors (for textures) is up to the application, so we provide callbacks.
  // (current version of the backend will only allocate one descriptor, future versions will need to allocate more)
  init_info.SrvDescriptorHeap    = render_device->GetBindlessDescriptorHeaps()[0];
  init_info.SrvDescriptorAllocFn = []( ImGui_ImplDX12_InitInfo* init_info,
                                       D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu_handle,
                                       D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu_handle )
  {
    RenderDevice const* p_render_device = ( RenderDevice* )init_info->UserData;
    g_ImguiHandleMap[out_cpu_handle->ptr] =
        p_render_device->AllocateRawDescriptorHandle( out_cpu_handle, out_gpu_handle );
  };
  init_info.SrvDescriptorFreeFn =
      []( ImGui_ImplDX12_InitInfo* init_info, D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle, D3D12_GPU_DESCRIPTOR_HANDLE )
  {
    RenderDevice const* p_render_device = ( RenderDevice* )init_info->UserData;
    p_render_device->FreeHandle( g_ImguiHandleMap[cpu_handle.ptr] );
    g_ImguiHandleMap.erase( cpu_handle.ptr );
  };

  init_info.UserData = render_device;
  ImGui_ImplDX12_Init( &init_info );
}

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
  , m_DrawList{ m_RenderDevice.get(), m_GeometryManager.get(), RenderDevice::kNumFrames }
  , m_LightManager{ std::make_unique_for_overwrite<LightManager>() }
{
  m_TextureLoader = std::make_unique_for_overwrite<TextureLoader>();
  TextureLoader::Create( m_TextureLoader.get(), m_RenderDevice.get(), 3 );

  _ = m_World.GetECS()
          .component<RotatingModel>()
          .member<float>( "Speed", 0, offsetof( RotatingModel, Speed ) )
          .add( flecs::With, m_World.GetECS().component<Rotation>() );

  InitImGui( window_handle, m_RenderDevice.get() );

  m_FGBlackboard.add<PerFrameConstants>();
  m_FGBlackboard.add<Environment::GpuRepr>();
  m_FGBlackboard.add<FG::BackbufferInfo>( m_SwapchainFormat, kDepthFormat, m_WindowWidth, m_WindowHeight );
  m_FGBlackboard.add<DrawList::Batches>();
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
  ENSURE( RenderPass::DepthPrePass::Create( &m_DrawPrePass, m_RenderDevice.get(), kDepthFormat ) );
  ENSURE( RenderPass::OpaqueForward::Create(
      &m_RenderOpaqueMeshes,
      {
          .RenderDevice          = m_RenderDevice.get(),
          .RenderTargetFormat    = DirectX::MakeSRGB( m_SwapchainFormat ),
          .DepthStencilFormat    = kDepthFormat,
          .DependsOnDepthPrePass = true,
      } ) );

  ENSURE( RenderPass::GBuffer::Create( &m_UpdateGBuffer, m_RenderDevice.get(), kDepthFormat ) );
  ENSURE( RenderPass::OmniLightDeferred::Create(
      &m_RenderOmniLights, m_RenderDevice.get(), DirectX::MakeSRGB( m_SwapchainFormat ), kDepthFormat ) );
  ENSURE( RenderPass::SpotLightDeferred::Create(
      &m_RenderSpotLights, m_RenderDevice.get(), DirectX::MakeSRGB( m_SwapchainFormat ), kDepthFormat ) );
  ENSURE( RenderPass::ScreenSpaceLightDeferred::Create(
      &m_RenderScreenSpaceLighting, m_RenderDevice.get(), DirectX::MakeSRGB( m_SwapchainFormat ) ) );

  ENSURE( RenderPass::AlphaTestedForward::Create(
      &m_RenderAlphaTestedMeshes, m_RenderDevice.get(), DirectX::MakeSRGB( m_SwapchainFormat ), kDepthFormat ) );
  ENSURE( RenderPass::TransparencyForward::Create(
      &m_RenderTransparentMeshes, m_RenderDevice.get(), DirectX::MakeSRGB( m_SwapchainFormat ), kDepthFormat ) );
  ENSURE( RenderPass::Skybox::Create(
      &m_RenderBackground, m_RenderDevice.get(), DirectX::MakeSRGB( m_SwapchainFormat ), kDepthFormat ) );

  ENSURE( RenderPass::Atmosphere::Create( &m_UpdateAtmosphericSky, m_RenderDevice.get() ) );
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

  m_SceneRoot = m_World.GetECS().entity( "SceneRoot" ).insert( []( Translation&, Rotation&, Scale& ) {} );

  m_World.GetECS()
      .entity( "Sun" )
      .child_of( m_SceneRoot )
      .add<ShadowCaster>()
      .insert(
          [&]( Rotation& rotation, DirectionalLight& dl )
          {
            rotation = ( Rotation )DirectX::XMQuaternionRotationRollPitchYaw(
                DirectX::XMConvertToRadians( -45.0f ), DirectX::XMConvertToRadians( 45.0f ), 0.0f );
            dl.Intensity = 5.0f;
            dl.FarPlane  = 100.0f;
          } );

  m_World.GetECS()
      .entity( "OmniLight 0" )
      .child_of( m_SceneRoot )
      .add<ShadowCaster>()
      .insert(
          [&]( Translation& translation, OmniLight& ol )
          {
            translation  = { 15.0f, 2.0f, 12.0f };
            ol.Color     = Color32::Blue();
            ol.Intensity = 15.0f;
            ol.Range     = 10.0f;
          } );

  m_World.GetECS()
      .entity( "OmniLight 1" )
      .child_of( m_SceneRoot )
      .add<ShadowCaster>()
      .insert(
          [&]( Translation& translation, OmniLight& ol )
          {
            translation  = { 0.0f, 2.0f, 5.0f };
            ol.Color     = Color32::Green();
            ol.Intensity = 15.0f;
            ol.Range     = 10.0f;
          } );

  m_World.GetECS()
      .entity( "OmniLight 2" )
      .child_of( m_SceneRoot )
      .add<ShadowCaster>()
      .insert(
          [&]( Translation& translation, OmniLight& ol )
          {
            translation  = { -15.0f, 2.0f, -5.0f };
            ol.Color     = Color32::Red();
            ol.Intensity = 25.0f;
            ol.Range     = 10.0f;
          } );

  m_World.GetECS()
      .entity( "SpotLight" )
      .child_of( m_SceneRoot )
      .add<ShadowCaster>()
      .insert(
          [&]( Translation& translation, Rotation& rotation, SpotLight& sl, RotatingModel& rm )
          {
            translation = { -15.0f, 1.0f, -5.0f };
            rotation    = ( Rotation )DirectX::XMQuaternionRotationRollPitchYaw(
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
  _                      = m_ModelLoader->TryLoadModel( "Sponza.glb" )->child_of( m_SceneRoot ).set_name( "Scene" );

  flecs::entity const rm = m_World.GetECS()
                               .entity( "HelmetRotator" )
                               .child_of( m_SceneRoot )
                               .insert(
                                   []( Translation& translation, RotatingModel& rot_model, WorldBoundingBox& )
                                   {
                                     rot_model.Speed = 20.0f;
                                     translation     = { 0.0f, 1.0f, 5.0f };
                                   } );

  _ = m_ModelLoader->TryLoadModel( "DamagedHelmet.glb" )
          ->child_of( rm )
          .set_name( "DamagedHelmet" )
          .set<Scale>( { 0.3f, 0.3f, 0.3f } );

  _ = m_ModelLoader->TryLoadModel( "AlphaBlendModeTest.glb" )
          ->child_of( m_SceneRoot )
          .set_name( "AlphaBlendTest" )
          .set<Translation>( { 5.0f, 2.0f, 7.0f } );

  constexpr char const* kEnvMapFile = "OvercastSoil.hdr";
  ENSURE( Environment::TryLoadFrom( m_Environment.get(), m_RenderDevice.get(), m_TextureLoader.get(), kEnvMapFile ) );

  SetupRenderPasses();
  FG::Context::Create( &m_FGContext, m_RenderDevice.get() );

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

  // TODO: Remove function static variables.
  static SceneTree    scene_tree;
  static PickingGizmo picking_gizmo;

  scene_tree.Draw( m_SceneRoot );
  picking_gizmo.Draw( *m_Camera, scene_tree.GetSelected() );
  Inspector::Draw( &m_World.GetECS(), scene_tree.GetSelected() );


  // Rendering
  ImGui::Render();

  float const delta_seconds = m_PerfCounter->GetDeltaMilliSeconds() * 0.001f;

  m_TextureLoader->Update();

  DirectX::XMUINT2 const mouse_pos = Input::Instance().GetMousePosition();
  float const            mouse_dx  = ( ( float )mouse_pos.x - ( float )m_PrevMouse.x ) / ( float )m_WindowWidth;
  float const            mouse_dy  = ( ( float )mouse_pos.y - ( float )m_PrevMouse.y ) / ( float )m_WindowHeight;
  m_PrevMouse                      = mouse_pos;

  DirectX::XMFLOAT4 mouse_ndc      = {
    ( float )mouse_pos.x / ( float )m_WindowWidth,
    ( float )mouse_pos.y / ( float )m_WindowHeight,
    1.0f,
    1.0f,
  };

  mouse_ndc.x             = mouse_ndc.x * 2.0f - 1.0f;
  mouse_ndc.y             = 1.0f - mouse_ndc.y * 2.0f;

  ImGuiIO const& imgui_io = ImGui::GetIO();
  if ( not imgui_io.WantCaptureMouse )
  {
    if ( Input::Instance().IsLeftMouseReleased() )
    {
      DirectX::XMVECTOR ray_origin  = m_Camera->GetPosition();
      DirectX::XMVECTOR ray_dir     = XMVector4Transform( XMLoadFloat4( &mouse_ndc ), m_Camera->GetInvProj() );
      float             w           = DirectX::XMVectorGetW( ray_dir );
      ray_dir                       = DirectX::XMVectorScale( ray_dir, 1.0f / w );
      ray_dir                       = XMVector3Transform( ray_dir, m_Camera->GetInvView() );
      ray_dir                       = DirectX::XMVectorSubtract( ray_dir, ray_origin );
      ray_dir                       = DirectX::XMVector3Normalize( ray_dir );

      DirectX::XMVECTOR ray_rev_dir = DirectX::XMVectorNegate( ray_dir );

      flecs::entity     hit;
      float             closest_hit = FLT_MAX;

      //
      std::function<void( flecs::entity )> const hit_test = [&]( flecs::entity e )
      {
        WorldBoundingBox const* lt              = e.try_get_mut<WorldBoundingBox>();
        bool                    is_actual_bound = e.has<LocalBoundingBox>();
        if ( lt )
        {
          float dist;
          float rev_dist;
          if ( lt->AABB.Intersects( ray_origin, ray_dir, dist ) )
          {
            if ( is_actual_bound and dist > 0.1f and dist < closest_hit and
                 not lt->AABB.Intersects( ray_origin, ray_rev_dir, rev_dist ) )
            {
              hit         = e;
              closest_hit = dist;
            }

            e.children( hit_test );
          }
        }
      };

      m_SceneRoot.children( hit_test );

      scene_tree.SetSelected( hit );
    }

    if ( Input::Instance().IsRightMouseDown() )
      m_Camera->SetYawPitch(
          m_Camera->GetYaw() - DirectX::XM_PI * mouse_dx, m_Camera->GetPitch() - DirectX::XM_PIDIV2 * mouse_dy );
  }

  if ( not imgui_io.WantCaptureKeyboard )
  {
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
  }

  m_World.GetECS().each(
      [&]( Rotation& lt, RotatingModel const& rm )
      {
        lt = ( Rotation )DirectX::XMQuaternionMultiply(
            ( DirectX::XMVECTOR )lt,
            DirectX::XMQuaternionRotationAxis(
                DirectX::XMVectorSet( 0.0f, 1.0f, 0.0f, 0.0f ),
                DirectX::XMConvertToRadians( rm.Speed ) * delta_seconds ) );
      } );

  uint32_t light_index = 0;
  m_World.GetECS().each(
      [&]( flecs::entity const e, DirectionalLight const& )
      {
        if ( e.has<Sun>() )
        {
          m_UpdateAtmosphericSky.SetSun( light_index );
        }
        light_index++;
      } );

  m_World.Update( delta_seconds );

  Input::Instance().Update();
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

  LightManager::GpuInfo light_info = m_LightManager->PrepareFrame( *m_Camera, frame_idx );

  m_LightManager->RenderAllShadows( command_list.Get(), draw_list_info, *m_RenderTargetManager, *m_Camera, frame_idx );

  SRVHandle const materials_srv           = m_MaterialManager->PrepareFrame();

  m_FGBlackboard.get<DrawList::Batches>() = draw_list_info;
  m_FGBlackboard.get<PerFrameConstants>() = {
    .MaterialsBuffer = materials_srv,
    .Camera          = camera_cbv,
    .ConfigBuffer    = m_ConfigurationBuffer.GetCBVHandle(),
    .LightInfo       = light_info,
  };

  m_FGBlackboard.get<Environment::GpuRepr>() = m_Environment->Repr();

  auto const               atmosphere        = m_UpdateAtmosphericSky( &frame_graph, &m_FGBlackboard, frame_idx );

  FrameGraphResource       depth_buffer      = m_DrawPrePass( &frame_graph, m_FGBlackboard );

  FrameGraphResource       opaque_pass_fwd   = m_RenderOpaqueMeshes( &frame_graph, m_FGBlackboard, depth_buffer );

  auto const               gbuffer           = m_UpdateGBuffer( &frame_graph, m_FGBlackboard, depth_buffer );
  FrameGraphResource const omni_pass_rt      = m_RenderOmniLights( &frame_graph, m_FGBlackboard, gbuffer );
  FrameGraphResource const spot_pass_rt = m_RenderSpotLights( &frame_graph, m_FGBlackboard, gbuffer, omni_pass_rt );
  FrameGraphResource const opaque_pass_dfr =
      m_RenderScreenSpaceLighting( &frame_graph, m_FGBlackboard, gbuffer, spot_pass_rt );

  auto opaque_pass = RenderPass::RenderDepthData{
    .RenderTarget = g_UseDeferredRendering ? opaque_pass_dfr : opaque_pass_fwd,
    .DepthStencil = depth_buffer,
  };

  auto const alpha_tested      = m_RenderAlphaTestedMeshes( &frame_graph, m_FGBlackboard, opaque_pass );
  auto const transparency_pass = m_RenderTransparentMeshes( &frame_graph, m_FGBlackboard, alpha_tested );

  m_RenderBackground.UseProceduralAtmosphericSky = g_Debug.SkyMode == DebugConfig::kAtmosphere;
  auto const skybox_pass = m_RenderBackground( &frame_graph, m_FGBlackboard, transparency_pass, atmosphere.SkyViewLUT );

  auto const final_output = g_Debug.SkyMode == DebugConfig::kNone ? transparency_pass.RenderTarget : skybox_pass;

  auto const imgui_out    = frame_graph.addCallbackPass(
      "ImGUI",
      [&]( FrameGraph::Builder& builder, FrameGraphResource& rt )
      { rt = builder.write( final_output, FG::Attachment{ .Index = 0 } ); },
      []( FrameGraphResource const&, FrameGraphPassResources&, FG::Context const* context )
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
        data.RenderTarget = builder.read( imgui_out, FG::CopySrc{} );
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

  {
    ZoneScopedN( "FrameGraph Compile" );
    frame_graph.compile();
  }

  if ( g_OutputFrameGraph )
  {
    std::ofstream{ "fg.dot" } << frame_graph;
    std::ofstream f{ "fg.json" };
    frame_graph.debugOutput( f, JsonWriter{} );
  }

  {
    ZoneScopedN( "FrameGraph Execute" );
    frame_graph.execute( &m_FGContext, &m_FGContext );
  }

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

  m_WindowWidth                            = rect.right - rect.left;
  m_WindowHeight                           = rect.bottom - rect.top;

  m_FGBlackboard.get<FG::BackbufferInfo>() = FG::BackbufferInfo{
    .SwapchainFormat    = m_SwapchainFormat,
    .DepthStencilFormat = kDepthFormat,
    .Width              = m_WindowWidth,
    .Height             = m_WindowHeight,
  };

  m_RenderDevice->ResizeSwapchain( m_WindowWidth, m_WindowHeight );

  m_Camera->SetAspectRatio( ( float )m_WindowWidth / ( float )m_WindowHeight );

  swprintf_s( m_SprintfBuffer, L"Ember %ux%u", m_WindowWidth, m_WindowHeight );
  SetWindowText( m_WindowHandle, m_SprintfBuffer );
}
