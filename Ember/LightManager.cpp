#include "LightManager.hpp"

#include "ModelLoader.hpp"
#include "RenderTargetManager.hpp"
#include "Util/DataUtil.hpp"
#include "Util/Profiling.hpp"

Ember::OmniLightHandle::OmniLightHandle( uint16_t const inner, uint16_t const generation )
  : m_Inner{ inner }, m_Generation{ generation }
{}

uint16_t Ember::OmniLightHandle::GetIndex() const
{
  return m_Inner;
}

uint16_t Ember::OmniLightHandle::GetGeneration() const
{
  return m_Generation;
}

std::strong_ordering Ember::OmniLightHandle::operator<=>( OmniLightHandle const& other ) const
{
  std::strong_ordering const x = m_Generation <=> other.m_Generation;
  if ( x == 0 )
  {
    return m_Inner <=> other.m_Inner;
  }
  return x;
}

Ember::LightManager::LightManager(
    RenderDevice*               render_device,
    std::vector<Buffer>         buffers,
    ComPtr<ID3D12PipelineState> shadow_pipeline,
    ComPtr<ID3D12RootSignature> shadow_root_signature )
  : m_RenderDevice{ render_device }
  , m_ShadowRootSignature{ std::move( shadow_root_signature ) }
  , m_ShadowPipeline{ std::move( shadow_pipeline ) }
  , m_PointLightBuffers{ std::move( buffers ) }
  , m_DirtyFrames{ ( uint8_t )buffers.size() }
{
  for ( uint16_t i = 0; i < kMaxOmniLights; ++i )
  {
    m_IndirectionMap[i] = i + 1;
  }
  m_FreeHead = 0;
}

void Ember::LightManager::Create( LightManager* light_manager, RenderDevice* render_device, uint32_t const num_frames )
{
  std::vector<Buffer> buffers;
  buffers.reserve( num_frames );
  for ( uint32_t i = 0; i < num_frames; i++ )
  {
    buffers.push_back(
        render_device->CreateStorageBuffer( sizeof( OmniLight ) * kMaxOmniLights, sizeof( OmniLight ) ) );
  }

  ComPtr<ID3DBlob> shadow_vs;
  ERR_ABORT( D3DReadFileToBlob( L"OmniShadowVS.cso", &shadow_vs ) );

  ComPtr<ID3DBlob> shadow_ps;
  ERR_ABORT( D3DReadFileToBlob( L"OmniShadowPS.cso", &shadow_ps ) );

  D3D12_ROOT_SIGNATURE_FLAGS const root_signature_flags =
      D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
      D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED |
      D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED | D3D12_ROOT_SIGNATURE_FLAG_DENY_MESH_SHADER_ROOT_ACCESS |
      D3D12_ROOT_SIGNATURE_FLAG_DENY_AMPLIFICATION_SHADER_ROOT_ACCESS |
      D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
      D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS | D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS;

  CD3DX12_ROOT_PARAMETER1 root_parameter;
  root_parameter.InitAsConstants( 2 * sizeof( DirectX::XMMATRIX ) / 4 + sizeof( float ), 0 );

  CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC root_signature_desc;
  root_signature_desc.Init_1_1( 1, &root_parameter, 0, nullptr, root_signature_flags );

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
    .pInputElementDescs = &Vertex::kInputElementDesc[0],
    .NumElements        = 1,
  };

  // We scale everything with -z for Left-Handed to Right-Handed correction.
  // So, Front becomes Clockwise instead of the counter-clockwise used in the engine.
  CD3DX12_RASTERIZER_DESC2 rasterizer_desc{ D3D12_DEFAULT };
  rasterizer_desc.FrontCounterClockwise = FALSE;
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

  new ( light_manager ) LightManager{
    render_device,
    std::move( buffers ),
    std::move( shadow_pipeline ),
    std::move( shadow_root_sig ),
  };
}

void Ember::LightManager::SetDirty()
{
  m_DirtyFrames = ( uint8_t )m_PointLightBuffers.size();
}

Ember::OmniLightHandle Ember::LightManager::AddOmniLight(
    DirectX::XMFLOAT3 const position,
    float const             range,
    Color32 const           color,
    float const             intensity,
    float const             attenuation )
{

  ASSERT_M( m_PointLightCount < kMaxOmniLights, "All free locs exhausted" );

  uint16_t const true_index = m_PointLightCount;
  uint16_t const index      = m_FreeHead;

  m_FreeHead                = m_IndirectionMap[index];
  m_IndirectionMap[index]   = true_index;

  uint16_t const generation = m_Generation[true_index];

  m_PointLights[true_index] = {
    .Position    = position,
    .Range       = range,
    .Color       = color,
    .Intensity   = intensity,
    .Attenuation = attenuation,
    .ShadowMap   = {},
  };

  m_PointLightCount++;

  SetDirty();

  return OmniLightHandle{ index, generation };
}

Ember::OmniLightHandle Ember::LightManager::AddShadowingOmniLight(
    DirectX::XMFLOAT3 const position,
    float const             range,
    Color32 const           color,
    float const             intensity,
    float const             attenuation )
{

  ASSERT_M( m_PointLightCount < kMaxOmniLights, "All free locs exhausted" );

  // Relocate the location to allocate at.
  SwapTrueLocations( m_ShadowingPointLightCount, m_PointLightCount );

  uint16_t const        true_index = m_ShadowingPointLightCount;
  uint16_t const        index      = m_FreeHead;

  uint16_t const        generation = m_Generation[true_index];

  OmniLightHandle const handle{ index, generation };

  SRVHandle const       shadow_map = AllocateOmniShadow( handle );

  ASSERT( shadow_map );

  m_PointLights[true_index] = {
    .Position    = position,
    .Range       = range,
    .Color       = color,
    .Intensity   = intensity,
    .Attenuation = attenuation,
    .ShadowMap   = shadow_map,
  };

  m_FreeHead              = m_IndirectionMap[index];
  m_IndirectionMap[index] = true_index;
  m_PointLightCount++;
  m_ShadowingPointLightCount++;

  SetDirty();

  return handle;
}

void Ember::LightManager::SwapTrueLocations( uint16_t const first, uint16_t const second )
{
  if ( first == second ) return;

  auto const indirection_first  = std::ranges::find( m_IndirectionMap, first );
  auto const indirection_second = std::ranges::find( m_IndirectionMap, second );
  ASSERT( indirection_first != std::ranges::end( m_IndirectionMap ) );
  ASSERT( indirection_second != std::ranges::end( m_IndirectionMap ) );
  *indirection_first  = second;
  *indirection_second = first;

  std::swap( m_Generation[first], m_Generation[second] );
  std::swap( m_PointLights[first], m_PointLights[second] );
}

Ember::SRVHandle Ember::LightManager::AllocateOmniShadow( OmniLightHandle const point_light_idx )
{
  ASSERT( not m_OmniShadowsInUse.contains( point_light_idx ) );

  Texture tex;
  if ( m_OmniShadowCache.empty() )
  {
    tex = m_RenderDevice->CreateTextureCube( {
        .Format    = DXGI_FORMAT_D16_UNORM,
        .Side      = kOmniShadowResolution,
        .Usage     = TextureUsage::kDepthSample,
        .MipLevels = MipLevels::kBase,
        .InitState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
    } );
  }
  else
  {
    tex = m_OmniShadowCache.front();
    m_OmniShadowCache.pop();
  }

  SRVHandle const handle              = tex.GetSRVHandle();
  m_OmniShadowsInUse[point_light_idx] = std::move( tex );
  return handle;
}

void Ember::LightManager::FreeOmniShadow( OmniLightHandle const point_light_idx )
{
  if ( auto const it = m_OmniShadowsInUse.find( point_light_idx ); it != m_OmniShadowsInUse.end() )
  {
    m_OmniShadowCache.push( it->second );
    m_OmniShadowsInUse.erase( it );
    return;
  }

  UNREACHABLE_M( "Point Light should be actually allocated." );
}

void Ember::LightManager::Free( OmniLightHandle const point_light_handle )
{
  uint16_t const index      = point_light_handle.GetIndex();
  uint16_t const generation = point_light_handle.GetGeneration();

  uint16_t const true_index = m_IndirectionMap[index];

  ASSERT( m_Generation[true_index] == generation );

  m_Generation[true_index]++;

  uint16_t const last_point_light_idx = m_PointLightCount - 1;

  if ( true_index < m_ShadowingPointLightCount )
  {
    uint16_t const last_shadowing_idx = m_ShadowingPointLightCount - 1;
    // To pack all shadow casters together
    // Swap with last shadow caster
    SwapTrueLocations( true_index, last_shadowing_idx );
    // Then swap with last light
    SwapTrueLocations( last_shadowing_idx, last_point_light_idx );

    FreeOmniShadow( point_light_handle );
    m_PointLights[last_point_light_idx].ShadowMap = {};
  }
  else
  {
    // True index non-shadow casting.
    SwapTrueLocations( true_index, last_point_light_idx );
  }

  m_PointLightCount--;

  m_IndirectionMap[index] = m_FreeHead;
  m_FreeHead              = index;

  SetDirty();
}

Ember::SRVHandle Ember::LightManager::PrepareFrame( uint32_t const frame_index )
{
  if ( m_DirtyFrames )
  {
    m_PointLightBuffers[frame_index].Write( 0, sizeof( OmniLight ) * m_PointLightCount, DataOf( m_PointLights ) );
    m_DirtyFrames--;
  }

  return m_PointLightBuffers[frame_index].GetSRVHandle();
}

uint16_t Ember::LightManager::GetPointLightCount() const
{
  return m_PointLightCount;
}

uint16_t Ember::LightManager::GetShadowingPointLightCount() const
{
  return m_ShadowingPointLightCount;
}

void Ember::LightManager::RenderAllShadows(
    ID3D12GraphicsCommandList*      command_list,
    World const&                    world,
    RenderTargetManager const&      rtm,
    DirectX::BoundingFrustum const& camera_frustum ) const
{
  ZoneScoped;
  command_list->SetGraphicsRootSignature( m_ShadowRootSignature.Get() );
  command_list->SetPipelineState( m_ShadowPipeline.Get() );
  command_list->IASetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST );

  for ( auto& [handle, texture] : m_OmniShadowsInUse )
  {
    ZoneScopedN( "CheckShadow" );
    ASSERT( handle.GetGeneration() == m_Generation[handle.GetGeneration()] );
    uint32_t const   index = handle.GetIndex();
    OmniLight const& light = m_PointLights[index];
    ZoneValue( index );

    DirectX::BoundingSphere sphere_of_influence{ light.Position, light.Range };
    if ( camera_frustum.Contains( sphere_of_influence ) == DirectX::DISJOINT ) continue;

    RenderOmniShadow( command_list, world, rtm, light, texture );
  }
}

void Ember::LightManager::RenderOmniShadow(
    ID3D12GraphicsCommandList* command_list,
    World const&               world,
    RenderTargetManager const& rtm,
    OmniLight const&           point_light,
    Texture const&             texture )
{
  ZoneScoped;

  auto top_of_shadow_barrier = CD3DX12_RESOURCE_BARRIER::Transition(
      texture.GetTexture(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE );
  command_list->ResourceBarrier( 1, &top_of_shadow_barrier );

  rtm.ClearDepthStencilView( command_list, texture, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0 );

  D3D12_RECT const     scissor  = { 0, 0, kOmniShadowResolution, kOmniShadowResolution };
  D3D12_VIEWPORT const viewport = { 0, 0, kOmniShadowResolution, kOmniShadowResolution, 0.0f, 1.0f };

  command_list->RSSetScissorRects( 1, &scissor );
  command_list->RSSetViewports( 1, &viewport );

  // We use left handed just this once.

  DirectX::XMVECTOR const origin  = XMLoadFloat3( &point_light.Position );
  DirectX::XMVECTOR const up      = DirectX::XMVectorSet( 0.0f, 1.0f, 0.0f, 0.0f );
  DirectX::XMVECTOR const right   = DirectX::XMVectorSet( 1.0f, 0.0f, 0.0f, 0.0f );
  DirectX::XMVECTOR const forward = DirectX::XMVectorSet( 0.0f, 0.0f, 1.0f, 0.0f );

  DirectX::XMMATRIX       views[6];
  views[0] = DirectX::XMMatrixLookToLH( origin, right, up );
  views[1] = DirectX::XMMatrixLookToLH( origin, DirectX::XMVectorNegate( right ), up );
  views[2] = DirectX::XMMatrixLookToLH( origin, up, DirectX::XMVectorNegate( forward ) );
  views[3] = DirectX::XMMatrixLookToLH( origin, DirectX::XMVectorNegate( up ), forward );
  views[4] = DirectX::XMMatrixLookToLH( origin, forward, up );
  views[5] = DirectX::XMMatrixLookToLH( origin, DirectX::XMVectorNegate( forward ), up );

  // Projection
  DirectX::XMMATRIX projection = DirectX::XMMatrixPerspectiveFovLH( DirectX::XM_PIDIV2, 1.0, 0.1f, point_light.Range );

  DirectX::BoundingFrustum proj_frustum;
  DirectX::BoundingFrustum::CreateFromMatrix( proj_frustum, projection );

  float packed_data[4] = { point_light.Position.x, point_light.Position.y, point_light.Position.z, point_light.Range };
  command_list->SetGraphicsRoot32BitConstants(
      0, ByteSizeOf( packed_data ) / 4, packed_data, 2 * sizeof( DirectX::XMMATRIX ) / 4 );

  D3D12_DEPTH_STENCIL_VIEW_DESC desc = *texture.GetDepthStencilView();
  desc.Texture2DArray.ArraySize      = 1;
  for ( int view_idx = 0; view_idx < 6; view_idx++ )
  {
    desc.Texture2DArray.FirstArraySlice = view_idx;
    rtm.OMSetRenderTargets( command_list, 0, nullptr, nullptr, &texture, &desc );

    auto vp_matrix = XMMatrixMultiply( views[view_idx], projection );
    command_list->SetGraphicsRoot32BitConstants(
        0, sizeof( DirectX::XMMATRIX ) / 4, &vp_matrix, sizeof( DirectX::XMMATRIX ) / 4 );

    DirectX::BoundingFrustum view_frustum;
    proj_frustum.Transform( view_frustum, XMMatrixInverse( nullptr, views[view_idx] ) );
    world.RenderShadow( command_list, view_frustum );
  }

  auto bottom_of_shadow_barrier = CD3DX12_RESOURCE_BARRIER::Transition(
      texture.GetTexture(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE );
  command_list->ResourceBarrier( 1, &bottom_of_shadow_barrier );
}
