#include "DirectionLightManager.hpp"

#include <Graphics/RenderDevice.hpp>
#include <Util/DataUtil.hpp>
#include <Util/DirectXHeaders.hpp>
#include <Util/Profiling.hpp>
#include <format>
#include "Camera.hpp"
#include "ModelLoader.hpp"
#include "Scene.hpp"

#include <meshoptimizer.h>

#include <Util/StringUtil.hpp>

namespace
{
struct PackedData
{
  Ember::SRVHandle LightData;
  uint32_t         LightIdx;
  Ember::CBVHandle CameraBuffer;
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
        .Type      = TextureType::kDepthStencil,
        .MipLevels = MipLevels::kBase,
        .ArraySize = kNumCascades,
        .InitState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
    } ) );

    wchar_t buf[32];
    m_ActiveShadows.back().SetName( FormatTo( buf, L"Dir Shadow Map {}", m_AllocatedShadows ) );
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

  D3D12_ROOT_PARAMETER1 root_parameters[] = {
    RootConstantBuffer{ .Register = 0 },
    RootConstants{ .Register = 1, .SizeBytes = sizeof( DrawList::PerBatch ) },
    RootConstants{ .Register = 2, .SizeBytes = sizeof( UINT ) },
    RootConstants{ .Register = 3, .SizeBytes = kNumCascades * sizeof( DirectX::XMFLOAT4 ) },
  };

  auto shadow_root_sig = render_device->CreateRootSignature( { root_parameters, {} } );
  ENSURE( shadow_root_sig );

  auto shadow_pipeline = render_device->CreateGraphicsPipeline( {
      .RootSignature   = shadow_root_sig.Get(),
      .RasterizerDesc  = Rasterizer{ .CullMode = Rasterizer::CullMode::kFront },
      .AmpShaderName   = "DirShadowAS.cso",
      .MeshShaderName  = "DirShadowMS.cso",
      .PixelShaderName = "EmptyPS.cso",
      .DSVFormat       = DXGI_FORMAT_D16_UNORM,
      .DebugName       = "Dir Shadow Pipeline",
  } );

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
        m_RenderDevice->CreateStorageBuffer( U32ByteSizeOf( m_LightData ), StrideOf( m_LightData ) );
    wchar_t name[] = L"Dir Light Buffer 0";
    name[18]       = L'0' + ( wchar_t )frame_index;
    m_DataBuffers[frame_index].SetName( name );
  }
  m_DataBuffers[frame_index].Write( 0, ByteSizeOf( m_LightData ), DataOf( m_LightData ) );

  return { m_DataBuffers[frame_index].GetSRVHandle(), m_ShadowingLightCount, m_TotalLightCount };
}

void Ember::Internal::DirectionLightManager::RenderAllShadows(
    CommandList* command_list, DrawList::Batches const& draw_info, Buffer const& frame_constants )
{
  ZoneScoped;
  command_list->SetGraphicsRootSignature( m_RootSignature.Get() );
  command_list->SetPipelineState( m_Pipeline.Get() );
  command_list->IASetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST );

  command_list->RSSetScissorViewport( kDirShadowResolution, kDirShadowResolution );

  static std::vector<D3D12_RESOURCE_BARRIER> barriers;
  barriers.resize( m_AllocatedShadows );

  std::transform(
      m_ActiveShadows.begin(),
      m_ActiveShadows.begin() + m_AllocatedShadows,
      barriers.begin(),
      []( Texture const& tex )
      {
        auto const current_state = tex.GetCurrentState();
        auto const next_state    = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        tex.SetCurrentState( next_state );
        return CD3DX12_RESOURCE_BARRIER::Transition( tex.GetTexture(), current_state, next_state );
      } );
  if ( not barriers.empty() ) command_list->ResourceBarrier( barriers );

  for ( uint32_t index = 0; index < m_AllocatedShadows; index++ )
  {
    RenderDirShadow( command_list, draw_info, frame_constants, index );
  }

  std::transform(
      m_ActiveShadows.begin(),
      m_ActiveShadows.begin() + m_AllocatedShadows,
      barriers.begin(),
      []( Texture const& tex )
      {
        auto const current_state = tex.GetCurrentState();
        auto const next_state    = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        tex.SetCurrentState( next_state );
        return CD3DX12_RESOURCE_BARRIER::Transition( tex.GetTexture(), current_state, next_state );
      } );

  if ( not barriers.empty() ) command_list->ResourceBarrier( barriers );
}

void Ember::Internal::DirectionLightManager::RenderDirShadow(
    CommandList const*       command_list,
    DrawList::Batches const& draw_info,
    Buffer const&            frame_constants,
    uint32_t const           light_index ) const
{
  PIXScopedEvent( command_list->Get(), PIX_COLOR_DEFAULT, "Render Directional Shadow %u", light_index );
  ZoneScoped;

  Texture const&      texture   = m_ActiveShadows[light_index];
  DirLightRepr const& dir_light = m_LightData[light_index];

  command_list->ClearDepthStencilView( texture.GetTexture(), D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0 );

  auto const batch = draw_info.Opaque();

  command_list->OMSetRenderTargets( 0, nullptr, &texture );
  command_list->SetGraphicsRootConstantBuffer( 0, frame_constants );
  command_list->SetGraphicsRootConstants( 1, batch );
  command_list->SetGraphicsRootConstant( 2, light_index );
  command_list->SetGraphicsRootConstants( 3, dir_light.CascadeSph );
  command_list->DispatchMesh( { .X = batch.CommandsCount } );
}
