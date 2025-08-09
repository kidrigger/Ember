#include "LightManager.hpp"

#include "ModelLoader.hpp"
#include "RenderTargetManager.hpp"
#include "Util/DataUtil.hpp"

Ember::LightManager::PointLightHandle::PointLightHandle( uint16_t const inner, uint16_t const generation )
  : m_Inner{ inner }, m_Generation{ generation }
{}

uint16_t Ember::LightManager::PointLightHandle::GetInner() const
{
  return m_Inner;
}

uint16_t Ember::LightManager::PointLightHandle::GetGeneration() const
{
  return m_Generation;
}

Ember::LightManager::LightManager(
    Buffer                      point_shadow_proj,
    std::vector<Buffer>         buffers,
    std::vector<Texture>        textures,
    ComPtr<ID3D12PipelineState> shadow_pipeline,
    ComPtr<ID3D12RootSignature> shadow_root_signature )
  : m_ShadowRootSignature{ std::move( shadow_root_signature ) }
  , m_ShadowPipeline{ std::move( shadow_pipeline ) }
  , m_PointShadowProjection{ std::move( point_shadow_proj ) }
  , m_PointLightBuffers{ std::move( buffers ) }
  , m_PointShadowMaps{ std::move( textures ) }
  , m_DirtyFrames{ ( uint8_t )buffers.size() }
{
  for ( uint16_t i = 0; i < kMaxPointLights; ++i )
  {
    m_IndirectionMap[i] = i + 1;
  }
  m_FreeHead = 0;

  memset( DataOf( m_PointShadowMapOwner ), 0xFFFF, ByteSizeOf( m_PointShadowMapOwner ) );
}

void Ember::LightManager::Create(
    LightManager* light_manager, RenderDevice* render_device, uint32_t const num_frames, uint32_t const max_shadows )
{
  std::vector<Buffer> buffers;
  buffers.reserve( num_frames );
  for ( uint32_t i = 0; i < num_frames; i++ )
  {
    buffers.push_back(
        render_device->CreateStorageBuffer( sizeof( PointLight ) * kMaxPointLights, sizeof( PointLight ) ) );
  }

  DirectX::XMVECTOR const origin  = DirectX::XMVectorSet( 0.0f, 0.0f, 0.0f, 1.0f );
  DirectX::XMVECTOR const up      = DirectX::XMVectorSet( 0.0f, 1.0f, 0.0f, 0.0f );
  DirectX::XMVECTOR const right   = DirectX::XMVectorSet( 1.0f, 0.0f, 0.0f, 0.0f );
  DirectX::XMVECTOR const forward = DirectX::XMVectorSet( 0.0f, 0.0f, -1.0f, 0.0f );
  DirectX::XMMATRIX       projections[6];
  projections[0] = DirectX::XMMatrixLookToRH( origin, right, up );
  projections[1] = DirectX::XMMatrixLookToRH( origin, DirectX::XMVectorNegate( right ), up );
  projections[2] = DirectX::XMMatrixLookToRH( origin, up, forward );
  projections[3] =
      DirectX::XMMatrixLookToRH( origin, DirectX::XMVectorNegate( up ), DirectX::XMVectorNegate( forward ) );
  projections[4]           = DirectX::XMMatrixLookToRH( origin, forward, up );
  projections[5]           = DirectX::XMMatrixLookToRH( origin, DirectX::XMVectorNegate( forward ), up );

  Buffer projection_buffer = render_device->CreateConstantBuffer( ByteSizeOf( projections ) );
  projection_buffer.Write( 0, ByteSizeOf( projections ), DataOf( projections ) );

  std::vector<Texture> shadow_maps;
  shadow_maps.reserve( max_shadows );
  for ( uint32_t i = 0; i < max_shadows; i++ )
  {
    shadow_maps.push_back( render_device->CreateTextureCube( {
        .Format    = DXGI_FORMAT_D16_UNORM,
        .Side      = kOmniShadowResolution,
        .Usage     = TextureUsage::kDepthSample,
        .MipLevels = MipLevels::kBase,
        .InitState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
    } ) );
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
    std::move( projection_buffer ), std::move( buffers ),         std::move( shadow_maps ),
    std::move( shadow_pipeline ),   std::move( shadow_root_sig ),
  };
}

void Ember::LightManager::SetDirty()
{
  m_DirtyFrames = ( uint8_t )m_PointLightBuffers.size();
}

Ember::LightManager::PointLightHandle Ember::LightManager::AddPointLight(
    DirectX::XMFLOAT3 const position,
    float const             range,
    Color32 const           color,
    float const             intensity,
    float const             attenuation )
{

  ASSERT_M( m_PointLightCount < kMaxPointLights, "All free locs exhausted" );

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

  return PointLightHandle{ index, generation };
}

Ember::LightManager::PointLightHandle Ember::LightManager::AddShadowingPointLight(
    DirectX::XMFLOAT3 const position,
    float const             range,
    Color32 const           color,
    float const             intensity,
    float const             attenuation )
{

  ASSERT_M( m_PointLightCount < kMaxPointLights, "All free locs exhausted" );

  // Relocate the location to allocate at.
  SwapTrueLocations( m_ShadowingPointLightCount, m_PointLightCount );

  uint16_t const true_index = m_ShadowingPointLightCount;
  uint16_t const index      = m_FreeHead;

  uint16_t const generation = m_Generation[true_index];

  SRVHandle      shadow_map = AllocatePointShadowMap( index );

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

  return PointLightHandle{ index, generation };
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

Ember::SRVHandle Ember::LightManager::AllocatePointShadowMap( uint16_t const point_light_idx )
{
  uint32_t const len = CountOf( m_PointShadowMapOwner );
  for ( uint32_t i = 0; i < len; i++ )
  {
    if ( m_PointShadowMapOwner[i] == UINT16_MAX )
    {
      m_PointShadowMapOwner[i] = point_light_idx;
      return m_PointShadowMaps[i].GetSRVHandle();
    }
  }

  return {};
}

void Ember::LightManager::FreePointShadowMap( uint16_t const point_light_idx )
{
  uint32_t const len = CountOf( m_PointShadowMapOwner );
  for ( uint32_t i = 0; i < len; i++ )
  {
    if ( m_PointShadowMapOwner[i] == point_light_idx )
    {
      m_PointShadowMapOwner[i] = UINT16_MAX;
    }
  }

  UNREACHABLE_M( "Point Light should be actually allocated." );
}

void Ember::LightManager::Free( PointLightHandle const point_light_handle )
{
  uint16_t const index      = point_light_handle.GetInner();
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

    FreePointShadowMap( index );
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
    m_PointLightBuffers[frame_index].Write( 0, sizeof( PointLight ) * m_PointLightCount, DataOf( m_PointLights ) );
    m_DirtyFrames--;
  }

  return m_PointLightBuffers[frame_index].GetSRVHandle();
}

Ember::CBVHandle Ember::LightManager::GetOmniProjectionsHandle() const
{
  return m_PointShadowProjection.GetCBVHandle();
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
    ID3D12GraphicsCommandList* command_list, RenderCommandQueue const& rcq, RenderTargetManager const& rtm ) const
{
  command_list->SetGraphicsRootSignature( m_ShadowRootSignature.Get() );
  command_list->SetPipelineState( m_ShadowPipeline.Get() );
  command_list->IASetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST );

  uint32_t const len = CountOf( m_PointShadowMapOwner );
  for ( uint32_t i = 0; i < len; i++ )
  {
    if ( m_PointShadowMapOwner[i] == 0xFFFF ) continue;

    RenderOmniShadow( command_list, rcq, rtm, m_PointLights[m_PointShadowMapOwner[i]], m_PointShadowMaps[i] );
  }
}

void Ember::LightManager::RenderOmniShadow(
    ID3D12GraphicsCommandList* command_list,
    RenderCommandQueue const&  rcq,
    RenderTargetManager const& rtm,
    PointLight const&          point_light,
    Texture const&             texture )
{
  auto top_of_shadow_barrier = CD3DX12_RESOURCE_BARRIER::Transition(
      texture.GetTexture(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE );
  command_list->ResourceBarrier( 1, &top_of_shadow_barrier );

  rtm.ClearDepthStencilView( command_list, texture, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0 );

  D3D12_RECT const     scissor  = { 0, 0, kOmniShadowResolution, kOmniShadowResolution };
  D3D12_VIEWPORT const viewport = { 0, 0, kOmniShadowResolution, kOmniShadowResolution, 0.0f, 1.0f };

  command_list->RSSetScissorRects( 1, &scissor );
  command_list->RSSetViewports( 1, &viewport );

  // Anything in the 'view' domain needs to be corrected to reverse 'z'.
  // We don't touch anything else. The systems are all still Right Handed.
  DirectX::XMMATRIX lh_to_rh_correction = DirectX::XMMatrixScaling( 1.0f, 1.0f, -1.0f );

  DirectX::XMFLOAT3 lh_light_position   = point_light.Position;
  lh_light_position.z                   = -lh_light_position.z;

  DirectX::XMVECTOR const origin        = XMLoadFloat3( &lh_light_position );
  DirectX::XMVECTOR const up            = DirectX::XMVectorSet( 0.0f, 1.0f, 0.0f, 0.0f );
  DirectX::XMVECTOR const right         = DirectX::XMVectorSet( 1.0f, 0.0f, 0.0f, 0.0f );
  DirectX::XMVECTOR const forward       = DirectX::XMVectorSet( 0.0f, 0.0f, -1.0f, 0.0f );

  DirectX::XMMATRIX       views[6];
  views[0] = DirectX::XMMatrixLookToRH( origin, right, up );
  views[1] = DirectX::XMMatrixLookToRH( origin, DirectX::XMVectorNegate( right ), up );
  views[2] = DirectX::XMMatrixLookToRH( origin, up, DirectX::XMVectorNegate( forward ) );
  views[3] = DirectX::XMMatrixLookToRH( origin, DirectX::XMVectorNegate( up ), forward );
  views[4] = DirectX::XMMatrixLookToRH( origin, forward, up );
  views[5] = DirectX::XMMatrixLookToRH( origin, DirectX::XMVectorNegate( forward ), up );

  // Projection
  DirectX::XMMATRIX projection = DirectX::XMMatrixPerspectiveFovRH( DirectX::XM_PIDIV2, 1.0, 0.1f, point_light.Range );

  float packed_data[4] = { point_light.Position.x, point_light.Position.y, point_light.Position.z, point_light.Range };
  command_list->SetGraphicsRoot32BitConstants(
      0, ByteSizeOf( packed_data ) / 4, packed_data, 2 * sizeof( DirectX::XMMATRIX ) / 4 );

  D3D12_DEPTH_STENCIL_VIEW_DESC desc = *texture.GetDepthStencilView();
  desc.Texture2DArray.ArraySize      = 1;
  for ( int view_idx = 0; view_idx < 6; view_idx++ )
  {
    desc.Texture2DArray.FirstArraySlice = view_idx;
    rtm.OMSetRenderTargets( command_list, 0, nullptr, nullptr, &texture, &desc );

    auto vp_matrix = XMMatrixMultiply( lh_to_rh_correction, XMMatrixMultiply( views[view_idx], projection ) );
    command_list->SetGraphicsRoot32BitConstants( 0, sizeof( DirectX::XMMATRIX ) / 4, &vp_matrix, 0 );

    size_t const element_count = rcq.Count();
    for ( size_t i = 0; i < element_count; i++ )
    {
      command_list->IASetIndexBuffer( &rcq.Meshes[i]->IndexBuffer.GetIndexBufferView() );
      command_list->IASetVertexBuffers( 0, 1, &rcq.Meshes[i]->VertexBuffer.GetVertexBufferView() );

      command_list->SetGraphicsRoot32BitConstants(
          0, sizeof( DirectX::XMMATRIX ) / 4, &rcq.Transforms[i].Transform, sizeof( DirectX::XMMATRIX ) / 4 );

      command_list->DrawIndexedInstanced(
          rcq.Primitives[i].IndexCount, 1, rcq.Primitives[i].FirstIndex, rcq.Primitives[i].FirstVertex, 0 );
    }
  }

  auto bottom_of_shadow_barrier = CD3DX12_RESOURCE_BARRIER::Transition(
      texture.GetTexture(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE );
  command_list->ResourceBarrier( 1, &bottom_of_shadow_barrier );
}
