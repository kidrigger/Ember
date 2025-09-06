#include "OmniLightManager.hpp"

#include "Camera.hpp"
#include "ModelLoader.hpp"
#include "RenderTargetManager.hpp"
#include "Util/DataUtil.hpp"
#include "Util/Profiling.hpp"

namespace
{
struct PackedData
{
  Ember::DrawList::Info DrawList;
  DirectX::XMFLOAT3     Position;
  float                 FarPlane;
  Ember::CBVHandle      ProjViewHandle;
};

static_assert( sizeof( PackedData ) == 36 );
} // namespace

Ember::Internal::OmniLightManager::OmniLightManager(
    RenderDevice*               render_device,
    Buffer                      projection_buffer,
    std::vector<Buffer>         light_buffers,
    ComPtr<ID3D12PipelineState> shadow_pipeline,
    ComPtr<ID3D12RootSignature> shadow_root_signature )
  : m_RenderDevice{ render_device }
  , m_ShadowProjectionBuffer{ std::move( projection_buffer ) }
  , m_RootSignature{ std::move( shadow_root_signature ) }
  , m_Pipeline{ std::move( shadow_pipeline ) }
  , m_DataBuffers{ std::move( light_buffers ) }
  , m_DirtyFrames{ ( uint8_t )light_buffers.size() }
{
  for ( uint16_t i = 0; i < kMaxOmniLights; ++i )
  {
    m_IndirectionMap[i] = i + 1;
  }
  m_IndirectionFreeHead = 0;

  // We use left handed just this once.

  DirectX::XMVECTOR const origin  = DirectX::XMVectorSet( 0.0f, 0.0f, 0.0f, 1.0f );
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

  m_ShadowProjectionBuffer.Write( 0, ByteSizeOf( views ), DataOf( views ) );
}

void Ember::Internal::OmniLightManager::Create(
    OmniLightManager* light_manager, RenderDevice* render_device, uint32_t const num_frames )
{

  Buffer              proj_buffer = render_device->CreateConstantBuffer( 6 * sizeof( DirectX::XMMATRIX ) );

  std::vector<Buffer> buffers;
  buffers.reserve( num_frames );
  for ( uint32_t i = 0; i < num_frames; i++ )
  {
    buffers.push_back(
        render_device->CreateStorageBuffer( sizeof( OmniLight ) * kMaxOmniLights, sizeof( OmniLight ) ) );
  }

  ComPtr<ID3DBlob> shadow_amp_shader;
  ERR_ABORT( D3DReadFileToBlob( L"OmniShadowAS.cso", &shadow_amp_shader ) );

  ComPtr<ID3DBlob> shadow_mesh_shader;
  ERR_ABORT( D3DReadFileToBlob( L"OmniShadowMS.cso", &shadow_mesh_shader ) );

  ComPtr<ID3DBlob> shadow_pixel_shader;
  ERR_ABORT( D3DReadFileToBlob( L"OmniShadowPS.cso", &shadow_pixel_shader ) );

  D3D12_ROOT_SIGNATURE_FLAGS const root_signature_flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
                                                          D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED |
                                                          D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED |
                                                          D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
                                                          D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS |
                                                          D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
                                                          D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS;

  CD3DX12_ROOT_PARAMETER1 root_parameter;
  root_parameter.InitAsConstants( sizeof( PackedData ) / 4, 0 );

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

  // We scale everything with -z for Left-Handed to Right-Handed correction.
  // So, Front becomes Clockwise instead of the counter-clockwise used in the engine.
  CD3DX12_RASTERIZER_DESC2 rasterizer_desc{ D3D12_DEFAULT };
  rasterizer_desc.FrontCounterClockwise = FALSE;
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
  ERR_ABORT( shadow_pipeline->SetName( L"Omni Shadow Pipeline" ) );

  new ( light_manager ) OmniLightManager{
    render_device,
    std::move( proj_buffer ),
    std::move( buffers ),
    std::move( shadow_pipeline ),
    std::move( shadow_root_sig ),
  };
}

void Ember::Internal::OmniLightManager::SetDirty()
{
  m_DirtyFrames = ( uint8_t )m_DataBuffers.size();
}

Ember::OmniLightHandle Ember::Internal::OmniLightManager::AddOmniLight(
    DirectX::XMFLOAT3 const position,
    float const             range,
    Color32 const           color,
    float const             intensity,
    float const             attenuation )
{

  ASSERT_M( m_TotalLightCount < kMaxOmniLights, "All free locs exhausted" );

  uint16_t const true_index = m_TotalLightCount;
  uint16_t const index      = m_IndirectionFreeHead;

  m_IndirectionFreeHead     = m_IndirectionMap[index];
  m_IndirectionMap[index]   = true_index;

  uint16_t const generation = m_HandleGeneration[index];

  m_LightData[true_index]   = {
      .Position    = position,
      .Range       = range,
      .Color       = color,
      .Intensity   = intensity,
      .Attenuation = attenuation,
      .ShadowMap   = {},
  };

  m_TotalLightCount++;

  SetDirty();

  return OmniLightHandle{ index, generation };
}

Ember::OmniLightHandle Ember::Internal::OmniLightManager::AddShadowingOmniLight(
    DirectX::XMFLOAT3 const position,
    float const             range,
    Color32 const           color,
    float const             intensity,
    float const             attenuation )
{

  ASSERT_M( m_TotalLightCount < kMaxOmniLights, "All free locs exhausted" );

  // Relocate the location to allocate at.
  SwapTrueLocations( m_ShadowingLightCount, m_TotalLightCount );

  uint16_t const        true_index = m_ShadowingLightCount;
  uint16_t const        index      = m_IndirectionFreeHead;

  uint16_t const        generation = m_HandleGeneration[index];

  OmniLightHandle const handle{ index, generation };

  SRVHandle const       shadow_map = AllocateOmniShadow( handle );

  ASSERT( shadow_map );

  m_LightData[true_index] = {
    .Position    = position,
    .Range       = range,
    .Color       = color,
    .Intensity   = intensity,
    .Attenuation = attenuation,
    .ShadowMap   = shadow_map,
  };

  m_IndirectionFreeHead   = m_IndirectionMap[index];
  m_IndirectionMap[index] = true_index;
  m_TotalLightCount++;
  m_ShadowingLightCount++;

  SetDirty();

  return handle;
}

void Ember::Internal::OmniLightManager::SwapTrueLocations( uint16_t const first, uint16_t const second )
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

Ember::SRVHandle Ember::Internal::OmniLightManager::AllocateOmniShadow( OmniLightHandle const omni_light_idx )
{
  ASSERT( not m_ShadowsInUse.Contains( omni_light_idx ) );

  Texture tex;
  if ( m_ShadowCache.empty() )
  {
    tex = m_RenderDevice->CreateTextureCube( {
        .Format    = DXGI_FORMAT_D16_UNORM,
        .Side      = kOmniShadowResolution,
        .Usage     = TextureUsage::kDepthSample,
        .MipLevels = MipLevels::kBase,
        .InitState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
    } );
    wchar_t buf[36];
    swprintf_s( buf, L"Omni Shadow Map %llu", m_ShadowsInUse.Size() );
    tex.SetName( buf );
  }
  else
  {
    tex = m_ShadowCache.front();
    m_ShadowCache.pop();
  }

  SRVHandle const handle         = tex.GetSRVHandle();
  m_ShadowsInUse[omni_light_idx] = std::move( tex );
  return handle;
}

void Ember::Internal::OmniLightManager::FreeOmniShadow( OmniLightHandle const omni_light_idx )
{
  if ( auto it = m_ShadowsInUse.Find( omni_light_idx ); it != m_ShadowsInUse.end() )
  {
    m_ShadowCache.push( it->second );
    m_ShadowsInUse.Erase( it );
    return;
  }

  UNREACHABLE_M( "Point Light should be actually allocated." );
}

void Ember::Internal::OmniLightManager::Free( OmniLightHandle const omni_light_handle )
{
  uint16_t const index      = omni_light_handle.GetIndex();
  uint16_t const generation = omni_light_handle.GetGeneration();

  ASSERT( m_HandleGeneration[index] == generation );
  m_HandleGeneration[index]++;

  uint16_t const true_index          = m_IndirectionMap[index];

  uint16_t const last_omni_light_idx = m_TotalLightCount - 1;

  if ( true_index < m_ShadowingLightCount )
  {
    uint16_t const last_shadowing_idx = m_ShadowingLightCount - 1;
    // To pack all shadow casters together
    // Swap with last shadow caster
    SwapTrueLocations( true_index, last_shadowing_idx );
    // Then swap with last light
    SwapTrueLocations( last_shadowing_idx, last_omni_light_idx );

    FreeOmniShadow( omni_light_handle );
    m_LightData[last_omni_light_idx].ShadowMap = {};
  }
  else
  {
    // True index non-shadow casting.
    SwapTrueLocations( true_index, last_omni_light_idx );
  }

  m_TotalLightCount--;

  SetDirty();

  if ( m_HandleGeneration[index] == UINT16_MAX ) return; // Handle no longer usable.

  m_IndirectionMap[index] = m_IndirectionFreeHead;
  m_IndirectionFreeHead   = index;
}

Ember::SRVHandle Ember::Internal::OmniLightManager::PrepareFrame( uint32_t const frame_index )
{
  if ( m_DirtyFrames )
  {
    m_DataBuffers[frame_index].Write( 0, sizeof( OmniLight ) * m_TotalLightCount, DataOf( m_LightData ) );
    m_DirtyFrames--;
  }

  return m_DataBuffers[frame_index].GetSRVHandle();
}

uint16_t Ember::Internal::OmniLightManager::GetOmniLightCount() const
{
  return m_TotalLightCount;
}

uint16_t Ember::Internal::OmniLightManager::GetShadowingOmniLightCount() const
{
  return m_ShadowingLightCount;
}

void Ember::Internal::OmniLightManager::RenderAllShadows(
    ID3D12GraphicsCommandList6* command_list,
    DrawList::Batches const&    draw_list,
    RenderTargetManager const&  rtm,
    Camera const&               camera )
{
  ZoneScoped;

  DirectX::BoundingFrustum const& camera_frustum = camera.GetLastUpdatedFrustum();

  command_list->SetGraphicsRootSignature( m_RootSignature.Get() );
  auto bindless_desc_heaps = m_RenderDevice->GetBindlessDescriptorHeaps();
  command_list->SetDescriptorHeaps( CountOf( bindless_desc_heaps ), DataOf( bindless_desc_heaps ) );
  command_list->SetPipelineState( m_Pipeline.Get() );
  command_list->IASetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST );

  D3D12_RECT const     scissor  = { 0, 0, kOmniShadowResolution, kOmniShadowResolution };
  D3D12_VIEWPORT const viewport = { 0, 0, kOmniShadowResolution, kOmniShadowResolution, 0.0f, 1.0f };

  command_list->RSSetScissorRects( 1, &scissor );
  command_list->RSSetViewports( 1, &viewport );

  std::pmr::vector<CD3DX12_RESOURCE_BARRIER> barriers{ m_ShadowsInUse.Size(),
                                                       std::pmr::polymorphic_allocator( &m_BumpAlloc ) };
  auto&                                      textures = m_ShadowsInUse.Values();

  std::ranges::transform(
      textures,
      barriers.begin(),
      []( Texture const& tex )
      {
        return CD3DX12_RESOURCE_BARRIER::Transition(
            tex.GetTexture(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE );
      } );

  if ( not barriers.empty() ) command_list->ResourceBarrier( CountOf( barriers ), DataOf( barriers ) );

  for ( auto const& [handle, texture] : m_ShadowsInUse )
  {
    ZoneScopedN( "CheckOmniShadow" );
    uint32_t const index = handle.GetIndex();
    ASSERT( handle.GetGeneration() == m_HandleGeneration[index] );
    OmniLight const& light = m_LightData[index];
    ZoneValue( index );

    DirectX::BoundingSphere sphere_of_influence{ light.Position, light.Range };
    if ( camera_frustum.Contains( sphere_of_influence ) == DirectX::DISJOINT ) continue;

    RenderOmniShadow( command_list, draw_list, rtm, light, texture );
  }

  std::ranges::transform(
      textures,
      barriers.begin(),
      []( Texture const& tex )
      {
        return CD3DX12_RESOURCE_BARRIER::Transition(
            tex.GetTexture(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE );
      } );

  if ( not barriers.empty() ) command_list->ResourceBarrier( CountOf( barriers ), DataOf( barriers ) );
}

void Ember::Internal::OmniLightManager::RenderOmniShadow(
    ID3D12GraphicsCommandList6* command_list,
    DrawList::Batches const&    draw_list,
    RenderTargetManager const&  rtm,
    OmniLight const&            omni_light,
    Texture const&              texture ) const
{
  ZoneScoped;

  rtm.ClearDepthStencilView( command_list, texture, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0 );

  PackedData const packed_data{
    .DrawList       = draw_list.Opaque,
    .Position       = omni_light.Position,
    .FarPlane       = omni_light.Range,
    .ProjViewHandle = m_ShadowProjectionBuffer.GetCBVHandle(),
  };
  command_list->SetGraphicsRoot32BitConstants( 0, sizeof( PackedData ) / 4, &packed_data, 0 );

  rtm.OMSetRenderTargets( command_list, 0, nullptr, &texture );

  command_list->DispatchMesh( draw_list.Opaque.DrawCount, 1, 1 );
}
