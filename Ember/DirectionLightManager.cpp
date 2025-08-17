#include "DirectionLightManager.hpp"

#include "DebugInfo.hpp"
#include "ModelLoader.hpp"
#include "RenderDevice.hpp"
#include "RenderTargetManager.hpp"
#include "Scene.hpp"
#include "Util/DataUtil.hpp"
#include "Util/Profiling.hpp"

void Ember::Internal::DirectionLightManager::SetDirty()
{
  m_DirtyFrames = ( uint8_t )m_DataBuffers.size();
}

void Ember::Internal::DirectionLightManager::SwapTrueLocations( uint16_t const first, uint16_t const second )
{
  if ( first == second ) return;

  auto const indirection_first  = std::ranges::find( m_IndirectionMap, first );
  auto const indirection_second = std::ranges::find( m_IndirectionMap, second );
  ASSERT( indirection_first != std::ranges::end( m_IndirectionMap ) );
  ASSERT( indirection_second != std::ranges::end( m_IndirectionMap ) );
  *indirection_first  = second;
  *indirection_second = first;

  std::swap( m_LightData[first], m_LightData[second] );
}

Ember::SRVHandle Ember::Internal::DirectionLightManager::AllocateDirShadow( DirLightHandle const dir_light_idx )
{
  ASSERT( not m_ShadowsInUse.Contains( dir_light_idx ) );

  Texture tex;
  if ( m_ShadowCache.empty() )
  {
    tex = m_RenderDevice->CreateTexture2D( {
        .Format    = DXGI_FORMAT_D16_UNORM,
        .Width     = kDirShadowResolution,
        .Height    = kDirShadowResolution,
        .Usage     = TextureUsage::kDepthSample,
        .MipLevels = MipLevels::kBase,
        .ArraySize = kNumCascades,
        .InitState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
    } );
    wchar_t buf[36];
    swprintf_s( buf, L"Dir Shadow Map %llu", m_ShadowsInUse.Size() );
    tex.SetName( buf );
  }
  else
  {
    tex = m_ShadowCache.front();
    m_ShadowCache.pop();
  }

  SRVHandle const handle        = tex.GetSRVHandle();
  m_ShadowsInUse[dir_light_idx] = std::move( tex );
  return handle;
}

void Ember::Internal::DirectionLightManager::FreeDirShadow( DirLightHandle const dir_light_idx )
{
  if ( auto it = m_ShadowsInUse.Find( dir_light_idx ); it != m_ShadowsInUse.end() )
  {
    m_ShadowCache.push( it->second );
    m_ShadowsInUse.Erase( it );
    return;
  }

  UNREACHABLE_M( "Point Light should be actually allocated." );
}

Ember::Internal::DirectionLightManager::DirectionLightManager(
    RenderDevice* const         render_device,
    std::vector<Buffer>         data_buffers,
    ComPtr<ID3D12PipelineState> pipeline,
    ComPtr<ID3D12RootSignature> root_signature )
  : m_RenderDevice{ render_device }
  , m_RootSignature{ std::move( root_signature ) }
  , m_Pipeline{ std::move( pipeline ) }
  , m_DataBuffers{ std::move( data_buffers ) }
{
  for ( uint16_t i = 0; i < kMaxDirLights; ++i )
  {
    m_IndirectionMap[i] = i + 1;
  }
  m_IndirectionFreeHead = 0;
}

void Ember::Internal::DirectionLightManager::Create(
    DirectionLightManager* light_manager, RenderDevice* render_device, uint32_t const num_frames )
{
  std::vector<Buffer> data_buffers;
  for ( uint32_t i = 0; i < num_frames; i++ )
  {
    data_buffers.push_back(
        render_device->CreateStorageBuffer( sizeof( DirLight ) * kMaxDirLights, sizeof( DirLight ) ) );
  }

  ComPtr<ID3DBlob> shadow_vs;
  ERR_ABORT( D3DReadFileToBlob( L"DirShadowVS.cso", &shadow_vs ) );

  ComPtr<ID3DBlob> shadow_ps;
  ERR_ABORT( D3DReadFileToBlob( L"DirShadowPS.cso", &shadow_ps ) );

  D3D12_ROOT_SIGNATURE_FLAGS const root_signature_flags =
      D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
      D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED |
      D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED |
      D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS | D3D12_ROOT_SIGNATURE_FLAG_DENY_MESH_SHADER_ROOT_ACCESS |
      D3D12_ROOT_SIGNATURE_FLAG_DENY_AMPLIFICATION_SHADER_ROOT_ACCESS |
      D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
      D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS | D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS;

  CD3DX12_ROOT_PARAMETER1 root_parameters[2];
  root_parameters[0].InitAsConstants( sizeof( DirectX::XMMATRIX ) / 4, 0 );
  root_parameters[1].InitAsConstants( 2, 1 );

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

  D3D12_INPUT_LAYOUT_DESC input_layout = {
    .pInputElementDescs = DataOf( ShadowVertex::kInputElementDesc ),
    .NumElements        = CountOf( ShadowVertex::kInputElementDesc ),
  };

  CD3DX12_RASTERIZER_DESC2 rasterizer_desc{ D3D12_DEFAULT };
  rasterizer_desc.FrontCounterClockwise = TRUE;
  rasterizer_desc.CullMode              = D3D12_CULL_MODE_FRONT;

  struct PipelineStream
  {
    CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE       RootSignature;
    CD3DX12_PIPELINE_STATE_STREAM_INPUT_LAYOUT         InputLayout;
    CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY   PrimitiveTopologyType;
    CD3DX12_PIPELINE_STATE_STREAM_VS                   VS;
    CD3DX12_PIPELINE_STATE_STREAM_PS                   PS;
    CD3DX12_PIPELINE_STATE_STREAM_RASTERIZER2          Rasterizer;
    CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL_FORMAT DSVFormat;
  };

  PipelineStream pipeline_stream{
    .RootSignature         = shadow_root_sig.Get(),
    .InputLayout           = input_layout,
    .PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
    .VS                    = CD3DX12_SHADER_BYTECODE( shadow_vs.Get() ),
    .PS                    = CD3DX12_SHADER_BYTECODE( shadow_ps.Get() ),
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
    render_device,
    std::move( data_buffers ),
    std::move( shadow_pipeline ),
    std::move( shadow_root_sig ),
  };
}

Ember::DirLightHandle Ember::Internal::DirectionLightManager::AddDirLight(
    DirectX::XMFLOAT3 const direction, Color32 const color, float const intensity )
{
  ASSERT_M( m_TotalLightCount < kMaxDirLights, "All free locs exhausted" );

  uint16_t const    true_index = m_TotalLightCount;
  uint16_t const    index      = m_IndirectionFreeHead;

  uint16_t const    generation = m_HandleGeneration[index];

  DirectX::XMFLOAT3 normalized_dir;
  XMStoreFloat3( &normalized_dir, DirectX::XMVector3Normalize( XMLoadFloat3( &direction ) ) );

  m_LightData[true_index] = DirLight{
    .Direction = normalized_dir,
    .Color     = color,
    .Intensity = intensity,
    .ShadowMap = {},
  };

  m_IndirectionFreeHead   = m_IndirectionMap[index];
  m_IndirectionMap[index] = true_index;
  m_TotalLightCount++;

  SetDirty();

  return DirLightHandle{ index, generation };
}

Ember::DirLightHandle Ember::Internal::DirectionLightManager::AddShadowingDirLight(
    DirectX::XMFLOAT3 const direction, Color32 const color, float const intensity )
{
  ASSERT_M( m_TotalLightCount < kMaxDirLights, "All free locs exhausted" );

  // Relocate the location to allocate at.
  SwapTrueLocations( m_ShadowingLightCount, m_TotalLightCount );

  uint16_t const       true_index = m_ShadowingLightCount;
  uint16_t const       index      = m_IndirectionFreeHead;

  uint16_t const       generation = m_HandleGeneration[index];

  DirLightHandle const handle{ index, generation };

  SRVHandle const      shadow_map = AllocateDirShadow( handle );

  ASSERT( shadow_map );

  DirectX::XMFLOAT3 normalized_dir;
  XMStoreFloat3( &normalized_dir, DirectX::XMVector3Normalize( XMLoadFloat3( &direction ) ) );

  m_LightData[true_index] = {
    .Direction = normalized_dir,
    .Color     = color,
    .Intensity = intensity,
    .ShadowMap = shadow_map,
  };

  m_IndirectionFreeHead   = m_IndirectionMap[index];
  m_IndirectionMap[index] = true_index;
  m_TotalLightCount++;
  m_ShadowingLightCount++;

  SetDirty();

  return handle;
}

void Ember::Internal::DirectionLightManager::Free( DirLightHandle dir_light_handle )
{
  uint16_t const index      = dir_light_handle.GetIndex();
  uint16_t const generation = dir_light_handle.GetGeneration();

  ASSERT( m_HandleGeneration[index] == generation );
  m_HandleGeneration[index]++;

  uint16_t const true_index         = m_IndirectionMap[index];

  uint16_t const last_dir_light_idx = m_TotalLightCount - 1;

  if ( true_index < m_ShadowingLightCount )
  {
    uint16_t const last_shadowing_idx = m_ShadowingLightCount - 1;
    // To pack all shadow casters together
    // Swap with last shadow caster
    SwapTrueLocations( true_index, last_shadowing_idx );
    // Then swap with last light
    SwapTrueLocations( last_shadowing_idx, last_dir_light_idx );

    FreeDirShadow( dir_light_handle );
    m_LightData[last_dir_light_idx].ShadowMap = {};
  }
  else
  {
    // True index non-shadow casting.
    SwapTrueLocations( true_index, last_dir_light_idx );
  }

  m_TotalLightCount--;

  SetDirty();

  if ( m_HandleGeneration[index] == UINT16_MAX ) return; // Handle no longer usable.

  m_IndirectionMap[index] = m_IndirectionFreeHead;
  m_IndirectionFreeHead   = index;
}

Ember::SRVHandle Ember::Internal::DirectionLightManager::PrepareFrame( uint32_t const frame_index )
{
  if ( m_DirtyFrames )
  {
    m_DataBuffers[frame_index].Write( 0, sizeof( DirLight ) * m_TotalLightCount, DataOf( m_LightData ) );
    m_DirtyFrames--;
  }

  return m_DataBuffers[frame_index].GetSRVHandle();
}

uint16_t Ember::Internal::DirectionLightManager::GetDirLightCount() const
{
  return m_TotalLightCount;
}

uint16_t Ember::Internal::DirectionLightManager::GetShadowingDirLightCount() const
{
  return m_ShadowingLightCount;
}

void Ember::Internal::DirectionLightManager::RenderAllShadows(
    ID3D12GraphicsCommandList*      command_list,
    World const&                    world,
    RenderTargetManager const&      rtm,
    DirectX::BoundingFrustum const& camera_frustum,
    uint32_t const                  frame_idx )
{
  ZoneScoped;
  command_list->SetGraphicsRootSignature( m_RootSignature.Get() );
  auto bindless_desc_heaps = m_RenderDevice->GetBindlessDescriptorHeaps();
  command_list->SetDescriptorHeaps( CountOf( bindless_desc_heaps ), DataOf( bindless_desc_heaps ) );
  command_list->SetPipelineState( m_Pipeline.Get() );
  command_list->IASetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
  command_list->SetGraphicsRoot32BitConstant( 1, ( UINT )m_DataBuffers[frame_idx].GetSRVHandle(), 0 );

  world.ClearCull();

  for ( auto const& [handle, texture] : m_ShadowsInUse )
  {
    uint32_t const index = handle.GetIndex();
    ASSERT( handle.GetGeneration() == m_HandleGeneration[index] );
    DirLight* light = &m_LightData[index];

    RenderDirShadow( command_list, world, rtm, light, texture, camera_frustum, index );
  }
}

void Ember::Internal::DirectionLightManager::RenderDirShadow(
    ID3D12GraphicsCommandList*      command_list,
    World const&                    world,
    RenderTargetManager const&      rtm,
    DirLight*                       dir_light,
    Texture const&                  texture,
    DirectX::BoundingFrustum const& camera_frust,
    uint32_t const                  light_index )
{
  ZoneScoped;

  auto top_of_shadow_barrier = CD3DX12_RESOURCE_BARRIER::Transition(
      texture.GetTexture(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE );
  command_list->ResourceBarrier( 1, &top_of_shadow_barrier );

  rtm.ClearDepthStencilView( command_list, texture, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0 );

  D3D12_RECT const     scissor  = { 0, 0, kDirShadowResolution, kDirShadowResolution };
  D3D12_VIEWPORT const viewport = { 0, 0, kDirShadowResolution, kDirShadowResolution, 0.0f, 1.0f };

  command_list->RSSetScissorRects( 1, &scissor );
  command_list->RSSetViewports( 1, &viewport );

  // Setup Light-Space basis
  DirectX::XMVECTOR direction = XMLoadFloat3( &dir_light->Direction );
  ASSERT_M(
      fabsf( DirectX::XMVector3LengthSq( direction ).m128_f32[0] - 1.0f ) < FLT_EPSILON,
      "This should be normalized on set" );

  DirectX::FXMVECTOR camera_orientation = XMLoadFloat4( &camera_frust.Orientation );

  DirectX::XMVECTOR  ls_right           = XMVector3Rotate( kRight, camera_orientation );
  if ( float dot = DirectX::XMVector3Dot( ls_right, direction ).m128_f32[0]; dot > 0.99f )
  {
    // If cam_right and direction are aligned, we need to use -fwd;
    ls_right = DirectX::XMVector3Rotate( XMVectorNegate( kForward ), camera_orientation );
  }
  else if ( dot < -0.99f )
  {
    // If cam_right is opposing direction, we need to use fwd;
    ls_right = XMVector3Rotate( kForward, camera_orientation );
  }

  DirectX::FXMVECTOR ls_up = DirectX::XMVector3Normalize( DirectX::XMVector3Cross( ls_right, direction ) );
  ls_right                 = DirectX::XMVector3Cross( direction, ls_up );

  // Light-Space <-> World Space Orientations
  DirectX::FXMVECTOR world_to_ls_orientation =
      XMQuaternionRotationMatrix( DirectX::XMMatrixLookToRH( DirectX::XMVectorZero(), direction, ls_up ) );
  DirectX::FXMVECTOR ls_to_world_orientation = DirectX::XMQuaternionInverse( world_to_ls_orientation );

  float              cascades[kNumCascades + 1];
  for ( int i = 0; i <= kNumCascades; i++ )
  {
    float c_log = camera_frust.Near * pow( camera_frust.Far / camera_frust.Near, ( float )i / ( float )kNumCascades );
    float c_uni = std::lerp( camera_frust.Near, camera_frust.Far, ( float )i / ( float )kNumCascades );
    cascades[i] = std::lerp( c_log, c_uni, kCascadeLambda );
  }
  // Copy cascades to GPU
  memcpy( dir_light->Cascades, &cascades[1], ByteSizeOf( dir_light->Cascades ) );

  DirectX::BoundingOrientedBox bob[kNumCascades];
  DirectX::XMFLOAT3            corners[8];
  for ( int cascade_id = 0; cascade_id < kNumCascades; cascade_id++ )
  {
    DirectX::BoundingFrustum frustum = camera_frust;

    frustum.Near                     = cascades[cascade_id] - kCascadeOverlap;
    frustum.Far                      = cascades[cascade_id + 1] + kCascadeOverlap;

    frustum.GetCorners( corners );
    for ( auto& corner : corners )
    {
      XMStoreFloat3( &corner, DirectX::XMVector3Rotate( XMLoadFloat3( &corner ), world_to_ls_orientation ) );
    }
    DirectX::BoundingBox ls_bb;
    DirectX::BoundingBox::CreateFromPoints( ls_bb, CountOf( corners ), DataOf( corners ), StrideOf( corners ) );

    // The shadow map is centered here.
    //
    DirectX::XMVECTOR focus = XMLoadFloat3( &ls_bb.Center );
    focus                   = DirectX::XMVector3Rotate( focus, ls_to_world_orientation );
    DirectX::XMFLOAT3 focus_f3;
    XMStoreFloat3( &focus_f3, focus );

    // Create 'shadow camera view and projections
    DirectX::XMMATRIX view       = DirectX::XMMatrixLookToRH( focus, direction, ls_up );
    DirectX::XMMATRIX projection = DirectX::XMMatrixOrthographicRH(
        ls_bb.Extents.x * 2.0f, ls_bb.Extents.y * 2.0f, -ls_bb.Extents.z, ls_bb.Extents.z );

    ls_bb.Extents.z = 1e6;
    // Infinitely long cull for shadow.
    DirectX::XMFLOAT4 bob_orientation;
    XMStoreFloat4( &bob_orientation, ls_to_world_orientation );
    bob[cascade_id] = { focus_f3, ls_bb.Extents, bob_orientation };
    world.CullBox( bob[cascade_id], 1llu << cascade_id );

    DirectX::XMMATRIX proj_view             = XMMatrixMultiply( view, projection );
    dir_light->LightSpaceMatrix[cascade_id] = proj_view;
  }

  SetDirty();

  command_list->SetGraphicsRoot32BitConstant( 1, light_index, 1 );
  rtm.OMSetRenderTargets( command_list, 0, nullptr, &texture );

  uint64_t constexpr kCullMask = ( 1 << kNumCascades ) - 1;
  world.GetECS().each(
      [&]( WorldTransform const& wt, CullInfo const& cull_info, Mesh const& mesh )
      {
        if ( cull_info.AreAllCulled( kCullMask ) ) return;

        for ( Primitive const& primitive : mesh.Primitives )
        {
          DirectX::BoundingBox bb;
          primitive.AABB.Transform( bb, wt.Transform );
          bool is_culled = true;
          // Draw if not culled in any cascades.
          for ( auto const& cascade_box : bob )
          {
            bool const culled_in_cascade =
                ( cascade_box.Contains( bb ) == DirectX::DISJOINT or bb.Contains( cascade_box ) == DirectX::DISJOINT );
            is_culled = is_culled and culled_in_cascade;
          }
          if ( is_culled ) continue;

          // TODO: Move outside primitive loop. IB and VB are per-mesh.
          command_list->IASetIndexBuffer( &mesh.Geometry->IndexBuffer.GetIndexBufferView() );
          command_list->IASetVertexBuffers( 0, 1, &mesh.Geometry->ShadowVertexBuffer.GetVertexBufferView() );

          command_list->SetGraphicsRoot32BitConstants( 0, sizeof( DirectX::XMMATRIX ) / 4, &wt.Transform, 0 );

          DebugInfo::Instance().PushDrawCall( primitive.DrawInfo.IndexCount );
          command_list->DrawIndexedInstanced(
              primitive.DrawInfo.IndexCount,
              kNumCascades,
              primitive.DrawInfo.FirstIndex,
              primitive.DrawInfo.FirstVertex,
              0 );
        }
      } );

  auto bottom_of_shadow_barrier = CD3DX12_RESOURCE_BARRIER::Transition(
      texture.GetTexture(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE );
  command_list->ResourceBarrier( 1, &bottom_of_shadow_barrier );
}
