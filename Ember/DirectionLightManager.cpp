#include "DirectionLightManager.hpp"

#include <Graphics/RenderDevice.hpp>
#include <Util/DataUtil.hpp>
#include <Util/Profiling.hpp>
#include "Camera.hpp"
#include "ModelLoader.hpp"
#include "RenderTargetManager.hpp"
#include "Scene.hpp"

#include <meshoptimizer.h>

namespace
{
struct PackedData
{
  Ember::DrawList::Info DrawList;
  Ember::SRVHandle      LightData;
  uint32_t              LightIdx;
  Ember::CBVHandle      CameraBuffer;
};
} // namespace

Ember::SRVHandle Ember::Internal::DirectionLightManager::AllocateShadow()
{
  if ( m_AllocatedShadows == m_ActiveShadows.size() )
  {
    m_ActiveShadows.emplace_back( m_RenderDevice->CreateTexture2D( {
        .Format    = DXGI_FORMAT_D16_UNORM,
        .Width     = kDirShadowResolution,
        .Height    = kDirShadowResolution,
        .Usage     = TextureUsage::kDepthSample,
        .MipLevels = MipLevels::kBase,
        .ArraySize = kNumCascades,
        .InitState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
    } ) );

    wchar_t buf[32];
    swprintf_s( buf, L"Dir Shadow Map %u", m_AllocatedShadows );
    m_ActiveShadows.back().SetName( buf );
  }

  return m_ActiveShadows[m_AllocatedShadows++].GetSRVHandle();
}

void Ember::Internal::DirectionLightManager::ClearShadows()
{
  m_AllocatedShadows = 0;
}

Ember::Internal::DirectionLightManager::DirectionLightManager(
    RenderDevice* const         render_device,
    World* const                world,
    std::vector<Buffer>         data_buffers,
    ComPtr<ID3D12PipelineState> pipeline,
    ComPtr<ID3D12RootSignature> root_signature )
  : m_RenderDevice{ render_device }
  , m_World{ world }
  , m_RootSignature{ std::move( root_signature ) }
  , m_Pipeline{ std::move( pipeline ) }
  , m_DataBuffers{ std::move( data_buffers ) }
{
  m_LightQuery =
      m_World->GetECS().query_builder<WorldTransform const, DirectionalLight const>().without<ShadowCaster>().build();

  m_ShadowLightQuery =
      m_World->GetECS().query_builder<WorldTransform const, DirectionalLight const>().with<ShadowCaster>().build();
}

void Ember::Internal::DirectionLightManager::Create(
    DirectionLightManager* light_manager, RenderDevice* render_device, World* world, uint32_t const num_frames )
{
  std::vector<Buffer> data_buffers;
  for ( uint32_t i = 0; i < num_frames; i++ )
  {
    data_buffers.push_back(
        render_device->CreateStorageBuffer( sizeof( DirLightRepr ) * kMaxDirLights, sizeof( DirLightRepr ) ) );
  }

  ComPtr<ID3DBlob> shadow_amp_shader;
  ERR_ABORT( D3DReadFileToBlob( L"DirShadowAS.cso", &shadow_amp_shader ) );

  ComPtr<ID3DBlob> shadow_mesh_shader;
  ERR_ABORT( D3DReadFileToBlob( L"DirShadowMS.cso", &shadow_mesh_shader ) );

  ComPtr<ID3DBlob> shadow_pixel_shader;
  ERR_ABORT( D3DReadFileToBlob( L"EmptyPS.cso", &shadow_pixel_shader ) );

  D3D12_ROOT_SIGNATURE_FLAGS const root_signature_flags = D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED |
                                                          D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED |
                                                          D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS |
                                                          D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS |
                                                          D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
                                                          D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
                                                          D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS;

  CD3DX12_ROOT_PARAMETER1 root_parameters[2];
  root_parameters[0].InitAsConstants( sizeof( PackedData ) / 4, 0 );
  root_parameters[1].InitAsConstants( kNumCascades * sizeof( DirectX::XMFLOAT4 ) / 4, 1 );

  CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC root_signature_desc;
  root_signature_desc.Init_1_1(
      CountOf( root_parameters ), DataOf( root_parameters ), 0, nullptr, root_signature_flags );

  D3D_ROOT_SIGNATURE_VERSION root_signature_version = render_device->FetchHighestRootSignatureVersion();

  ComPtr<ID3DBlob>           root_signature_blob;
  ComPtr<ID3DBlob>           error_blob;
  ERR_ABORT( D3DX12SerializeVersionedRootSignature(
      &root_signature_desc, root_signature_version, &root_signature_blob, &error_blob ) );

  ComPtr<ID3D12RootSignature> shadow_root_sig;
  ERR_ABORT( render_device->GetDevice()->CreateRootSignature(
      0,
      root_signature_blob->GetBufferPointer(),
      root_signature_blob->GetBufferSize(),
      IID_PPV_ARGS( &shadow_root_sig ) ) );

  CD3DX12_RASTERIZER_DESC2 rasterizer_desc{ D3D12_DEFAULT };
  rasterizer_desc.FrontCounterClockwise = TRUE;
  rasterizer_desc.CullMode              = D3D12_CULL_MODE_FRONT;

  struct PipelineStream
  {
    CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE       RootSignature;
    CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY   PrimitiveTopologyType;
    CD3DX12_PIPELINE_STATE_STREAM_AS                   AS;
    CD3DX12_PIPELINE_STATE_STREAM_MS                   MS;
    CD3DX12_PIPELINE_STATE_STREAM_PS                   PS;
    CD3DX12_PIPELINE_STATE_STREAM_RASTERIZER2          Rasterizer;
    CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL_FORMAT DSVFormat;
  };

  PipelineStream pipeline_stream{
    .RootSignature         = shadow_root_sig.Get(),
    .PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
    .AS                    = CD3DX12_SHADER_BYTECODE( shadow_amp_shader.Get() ),
    .MS                    = CD3DX12_SHADER_BYTECODE( shadow_mesh_shader.Get() ),
    .PS                    = CD3DX12_SHADER_BYTECODE( shadow_pixel_shader.Get() ),
    .Rasterizer            = rasterizer_desc,
    .DSVFormat             = DXGI_FORMAT_D16_UNORM,
  };

  D3D12_PIPELINE_STATE_STREAM_DESC desc{
    .SizeInBytes                   = sizeof( pipeline_stream ),
    .pPipelineStateSubobjectStream = &pipeline_stream,
  };

  ComPtr<ID3D12PipelineState> shadow_pipeline;
  ERR_ABORT( render_device->GetDevice()->CreatePipelineState( &desc, IID_PPV_ARGS( &shadow_pipeline ) ) );
  ERR_ABORT( shadow_pipeline->SetName( L"Dir Shadow Pipeline" ) );

  new ( light_manager ) DirectionLightManager{
    render_device, world, std::move( data_buffers ), std::move( shadow_pipeline ), std::move( shadow_root_sig ),
  };
}

void Ember::Internal::DirectionLightManager::CalculateShadowParameters(
    DirectX::BoundingFrustum const& camera_frustum, DirLightRepr* dir_light )
{
  // Setup Light-Space basis
  DirectX::XMVECTOR      direction = XMLoadFloat3( &dir_light->Direction );
  [[maybe_unused]] float len       = DirectX::XMVectorGetX( DirectX::XMVector3LengthSq( direction ) );
  ASSERT_M( std::abs( len - 1.0f ) < 1e-5, "This should be normalized on set" );

  DirectX::XMVECTOR ls_right = kRight;
  if ( float dot = DirectX::XMVectorGetX( DirectX::XMVector3Dot( ls_right, direction ) ); dot > 0.99f )
  {
    // If cam_right and direction are aligned, we need to use -fwd;
    ls_right = XMVectorNegate( kForward );
  }
  else if ( dot < -0.99f )
  {
    // If cam_right is opposing direction, we need to use fwd;
    ls_right = kForward;
  }

  DirectX::FXMVECTOR ls_up = DirectX::XMVector3Normalize( DirectX::XMVector3Cross( ls_right, direction ) );
  ls_right                 = DirectX::XMVector3Cross( direction, ls_up );

  // Light-Space <-> World Space Orientations
  DirectX::FXMVECTOR world_to_ls_orientation =
      XMQuaternionRotationMatrix( DirectX::XMMatrixLookToRH( DirectX::XMVectorZero(), direction, ls_up ) );

  float cascades[kNumCascades + 1];
  for ( int i = 0; i <= kNumCascades; i++ )
  {
    float c_log =
        camera_frustum.Near * pow( camera_frustum.Far / camera_frustum.Near, ( float )i / ( float )kNumCascades );
    float c_uni = std::lerp( camera_frustum.Near, camera_frustum.Far, ( float )i / ( float )kNumCascades );
    cascades[i] = std::lerp( c_log, c_uni, kCascadeLambda );
  }

  DirectX::XMFLOAT4 cull_params[kNumCascades];
  for ( int cascade_id = 0; cascade_id < kNumCascades; cascade_id++ )
  {
    DirectX::BoundingFrustum frustum = camera_frustum;

    frustum.Near                     = cascades[cascade_id] - kCascadeOverlap;
    frustum.Far                      = cascades[cascade_id + 1] + kCascadeOverlap;

    DirectX::BoundingSphere ws_bs;
    DirectX::BoundingSphere::CreateFromFrustum( ws_bs, frustum );

    float world_per_texel = ( 2.0f * ws_bs.Radius ) / kDirShadowResolution;
    float texel_per_world = 1.0f / world_per_texel;

    // The shadow map is centered here.
    // Rounding the focus to texel increments to keep the
    DirectX::FXMVECTOR focus = DirectX::XMVector3InverseRotate(
        DirectX::XMVectorScale(
            DirectX::XMVectorFloor( DirectX::XMVectorScale(
                DirectX::XMVector3Rotate( XMLoadFloat3( &ws_bs.Center ), world_to_ls_orientation ), texel_per_world ) ),
            world_per_texel ),
        world_to_ls_orientation );

    DirectX::XMFLOAT3 focus_v3;
    XMStoreFloat3( &focus_v3, focus );

    // Create 'shadow camera view and projections
    DirectX::FXMMATRIX view = DirectX::XMMatrixLookToRH( focus, direction, ls_up );
    DirectX::FXMMATRIX projection =
        DirectX::XMMatrixOrthographicRH( ws_bs.Radius * 2.0f, ws_bs.Radius * 2.0f, -ws_bs.Radius, ws_bs.Radius );
    ASSERT_M(
        projection.r[0].m128_f32[0] == projection.r[1].m128_f32[1],
        "The Amplification shader expects a square projection" );

    cull_params[cascade_id]                 = { focus_v3.x, focus_v3.y, focus_v3.z, ws_bs.Radius };

    DirectX::FXMMATRIX proj_view            = XMMatrixMultiply( view, projection );
    dir_light->LightSpaceMatrix[cascade_id] = proj_view;
    dir_light->CascadeSph[cascade_id]       = cull_params[cascade_id];
  }
}

Ember::LightInfo Ember::Internal::DirectionLightManager::PrepareFrame(
    Camera const& camera, uint32_t const frame_index )
{
  m_LightData.clear();
  ClearShadows();

  DirectX::BoundingFrustum camera_frust = camera.GetLastUpdatedFrustum();

  // Camera is right-handed but with DXMath, Near is positive, Far is negative
  float const default_far_plane = std::min( std::abs( camera_frust.Near ), 150.0f );

  m_ShadowLightQuery.each(
      [&]( WorldTransform const& transform, DirectionalLight const& light )
      {
        DirectX::XMFLOAT3 direction;
        XMStoreFloat3(
            &direction,
            DirectX::XMVector3Normalize(
                XMVector3Rotate( kForward, XMQuaternionRotationMatrix( transform.Transform ) ) ) );
        float const far_plane = light.FarPlane > 0.0f ? light.FarPlane : default_far_plane;

        m_LightData.push_back( {
            .Direction = direction,
            .Color     = light.Color,
            .Intensity = light.Intensity,
            .ShadowMap = AllocateShadow(),
            .FarPlane  = far_plane,
        } );

        // Camera is right-handed but with DXMath, Near is positive, Far is negative
        camera_frust.Near = -far_plane;
        CalculateShadowParameters( camera_frust, &m_LightData.back() );
      } );

  m_ShadowingLightCount = ( uint32_t )m_LightData.size();

  m_LightQuery.each(
      [&]( WorldTransform const& transform, DirectionalLight const& light )
      {
        DirectX::XMFLOAT3 direction;
        XMStoreFloat3(
            &direction,
            DirectX::XMVector3Normalize(
                XMVector3Rotate( kForward, XMQuaternionRotationMatrix( transform.Transform ) ) ) );

        m_LightData.push_back( {
            .Direction = direction,
            .Color     = light.Color,
            .Intensity = light.Intensity,
        } );
      } );

  m_TotalLightCount = ( uint32_t )m_LightData.size();

  if ( m_DataBuffers[frame_index].GetSize() < m_TotalLightCount * sizeof( DirLightRepr ) )
  {
    m_DataBuffers[frame_index] =
        m_RenderDevice->CreateStorageBuffer( ByteSizeOf( m_LightData ), StrideOf( m_LightData ) );
    wchar_t name[] = L"Dir Light Buffer 0";
    name[18]       = L'0' + ( wchar_t )frame_index;
    m_DataBuffers[frame_index].SetName( name );
  }
  m_DataBuffers[frame_index].Write( 0, ByteSizeOf( m_LightData ), DataOf( m_LightData ) );

  return { m_DataBuffers[frame_index].GetSRVHandle(), m_ShadowingLightCount, m_TotalLightCount };
}

void Ember::Internal::DirectionLightManager::RenderAllShadows(
    ID3D12GraphicsCommandList6* command_list,
    DrawList::Batches const&    draw_info,
    RenderTargetManager const&  rtm,
    Camera const&               camera,
    uint32_t const              frame_idx )
{
  ZoneScoped;
  command_list->SetGraphicsRootSignature( m_RootSignature.Get() );
  command_list->SetPipelineState( m_Pipeline.Get() );
  command_list->IASetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST );

  D3D12_RECT const     scissor  = { 0, 0, kDirShadowResolution, kDirShadowResolution };
  D3D12_VIEWPORT const viewport = { 0, 0, kDirShadowResolution, kDirShadowResolution, 0.0f, 1.0f };

  command_list->RSSetScissorRects( 1, &scissor );
  command_list->RSSetViewports( 1, &viewport );

  static std::vector<CD3DX12_RESOURCE_BARRIER> barriers;
  barriers.resize( m_AllocatedShadows );

  std::transform(
      m_ActiveShadows.begin(),
      m_ActiveShadows.begin() + m_AllocatedShadows,
      barriers.begin(),
      []( Texture const& tex )
      {
        return CD3DX12_RESOURCE_BARRIER::Transition(
            tex.GetTexture(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE );
      } );
  if ( not barriers.empty() ) command_list->ResourceBarrier( CountOf( barriers ), DataOf( barriers ) );

  for ( uint32_t index = 0; index < m_AllocatedShadows; index++ )
  {
    RenderDirShadow( command_list, draw_info, rtm, camera, frame_idx, index );
  }

  std::transform(
      m_ActiveShadows.begin(),
      m_ActiveShadows.begin() + m_AllocatedShadows,
      barriers.begin(),
      []( Texture const& tex )
      {
        return CD3DX12_RESOURCE_BARRIER::Transition(
            tex.GetTexture(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE );
      } );

  if ( not barriers.empty() ) command_list->ResourceBarrier( CountOf( barriers ), DataOf( barriers ) );
}

void Ember::Internal::DirectionLightManager::RenderDirShadow(
    ID3D12GraphicsCommandList6* command_list,
    DrawList::Batches const&    draw_info,
    RenderTargetManager const&  rtm,
    Camera const&               camera,
    uint32_t const              frame_index,
    uint32_t const              light_index )
{
  PIXScopedEvent( command_list, PIX_COLOR_DEFAULT, "Render Directional Shadow %u", light_index );
  ZoneScoped;

  Texture&      texture   = m_ActiveShadows[light_index];
  DirLightRepr& dir_light = m_LightData[light_index];

  rtm.ClearDepthStencilView( command_list, texture, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0 );

  PackedData packed_data{
    .DrawList     = draw_info.Opaque,
    .LightData    = m_DataBuffers[frame_index].GetSRVHandle(),
    .LightIdx     = light_index,
    .CameraBuffer = camera.GetLastUpdatedBuffer(),
  };

  command_list->SetGraphicsRoot32BitConstants( 0, sizeof( packed_data ) / 4, &packed_data, 0 );
  rtm.OMSetRenderTargets( command_list, 0, nullptr, &texture );

  // TODO: Alpha tested + Blended
  command_list->SetGraphicsRoot32BitConstants(
      1, ByteSizeOf( dir_light.CascadeSph ) / 4, DataOf( dir_light.CascadeSph ), 0 );
  command_list->DispatchMesh( draw_info.Opaque.DrawCount, 1, 1 );
}
