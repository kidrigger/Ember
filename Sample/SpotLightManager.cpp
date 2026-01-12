#include "SpotLightManager.hpp"

#include "Camera.hpp"

#include <Util/DataUtil.hpp>
#include <Util/Profiling.hpp>

namespace
{
struct PackedData
{
  Ember::SRVHandle LightBuffer;
  uint32_t         LightIndex;
};

static_assert( sizeof( PackedData ) == 8 );
} // namespace

Ember::SRVHandle Ember::Internal::SpotLightManager::AllocateSpotShadow()
{
  if ( m_AllocatedShadows == m_ActiveShadows.size() )
  {
    m_ActiveShadows.emplace_back( m_RenderDevice->CreateTexture2D( {
        .Format    = DXGI_FORMAT_D16_UNORM,
        .Width     = kSpotShadowResolution,
        .Height    = kSpotShadowResolution,
        .Usage     = TextureUsage::kDepthStencil,
        .MipLevels = 1,
        .InitState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
    } ) );

    wchar_t buf[32];
    swprintf_s( buf, L"Spot Shadow Map %u", m_AllocatedShadows );
    m_ActiveShadows.back().SetName( buf );
  }

  return m_ActiveShadows[m_AllocatedShadows++].GetSRVHandle();
}

void Ember::Internal::SpotLightManager::ClearShadows()
{
  m_AllocatedShadows = 0;
}

float Ember::Internal::SpotLightManager::CalculateRange( Color32 const color, float const intensity )
{
  auto [x, y, z]       = color.UnpackRgb();
  float const max_comp = std::max( x, std::max( y, z ) );
  return sqrt( ( max_comp * intensity ) ) * 10.0f;
}

Ember::Internal::SpotLightManager::SpotLightManager(
    RenderDevice*               render_device,
    World*                      world,
    std::vector<Buffer>         data_buffers,
    ComPtr<ID3D12PipelineState> shadow_pipeline,
    ComPtr<ID3D12RootSignature> shadow_root_signature )
  : m_RenderDevice{ render_device }
  , m_World{ world }
  , m_RootSignature{ std::move( shadow_root_signature ) }
  , m_Pipeline{ std::move( shadow_pipeline ) }
  , m_DataBuffers{ std::move( data_buffers ) }
  , m_AllocatedShadows{ 0 }
{
  m_ShadowLightQuery =
      m_World->GetECS().query_builder<WorldTransform const, SpotLight const>().with<ShadowCaster const>().build();
  m_LightQuery =
      m_World->GetECS().query_builder<WorldTransform const, SpotLight const>().without<ShadowCaster const>().build();
}

void Ember::Internal::SpotLightManager::Create(
    SpotLightManager* light_manager, RenderDevice* render_device, World* world, uint32_t const num_frames )
{
  std::vector<Buffer> data_buffers( num_frames );

  for ( uint32_t frame_index = 0; frame_index < num_frames; ++frame_index )
  {
    data_buffers[frame_index] =
        render_device->CreateStorageBuffer( 8 * sizeof( SpotLightRepr ), sizeof( SpotLightRepr ) );
    wchar_t name[] = L"Spot Light Buffer 0";
    name[18]       = L'0' + ( wchar_t )frame_index;
    data_buffers[frame_index].SetName( name );
  }

  ComPtr<ID3DBlob> shadow_amp_shader;
  ERR_ABORT( D3DReadFileToBlob( L"SpotShadowAS.cso", &shadow_amp_shader ) );

  ComPtr<ID3DBlob> shadow_mesh_shader;
  ERR_ABORT( D3DReadFileToBlob( L"SpotShadowMS.cso", &shadow_mesh_shader ) );

  ComPtr<ID3DBlob> shadow_pixel_shader;
  ERR_ABORT( D3DReadFileToBlob( L"SpotShadowPS.cso", &shadow_pixel_shader ) );

  D3D12_ROOT_SIGNATURE_FLAGS const root_signature_flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
                                                          D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED |
                                                          D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED |
                                                          D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
                                                          D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS |
                                                          D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
                                                          D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS;

  CD3DX12_ROOT_PARAMETER1 root_parameters[2];
  root_parameters[0].InitAsConstants( sizeof( DrawList::PerBatch ) / 4, 0 );
  root_parameters[1].InitAsConstants( sizeof( PackedData ) / 4, 1 );

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
  ERR_ABORT( shadow_pipeline->SetName( L"Spot Shadow Pipeline" ) );

  new ( light_manager ) SpotLightManager{
    render_device, world, std::move( data_buffers ), std::move( shadow_pipeline ), std::move( shadow_root_sig ),
  };
}

Ember::LightInfo Ember::Internal::SpotLightManager::PrepareFrame( uint32_t const frame_index )
{
  m_LightData.clear();
  ClearShadows();

  m_ShadowLightQuery.each(
      [&]( WorldTransform const& transform, SpotLight const& light )
      {
        DirectX::XMFLOAT3 const position = transform.GetTranslation();
        float const       range = light.Range > 0.0f ? light.Range : CalculateRange( light.Color, light.Intensity );
        DirectX::XMFLOAT3 direction;
        XMStoreFloat3(
            &direction,
            DirectX::XMVector3Normalize( DirectX::XMVector3Rotate(
                DirectX::XMVectorSet( 0.0f, 0.0f, -1.0f, 0.0f ),
                XMQuaternionRotationMatrix( transform.Transform ) ) ) );

        DirectX::XMMATRIX const view_mat = XMMatrixLookToRH(
            XMLoadFloat3( &position ), XMLoadFloat3( &direction ), DirectX::XMVECTORF32{ 0.0, 1.0f, 0.0f, 0.0f } );
        DirectX::XMMATRIX const proj_mat =
            DirectX::XMMatrixPerspectiveFovRH( light.ConeOuterHalfAngle * 2.0f, 1.0f, 0.01f, range );

        DirectX::XMFLOAT4X4 light_mat;
        XMStoreFloat4x4( &light_mat, XMMatrixMultiply( view_mat, proj_mat ) );

        m_LightData.push_back( {
            .LightMatrix     = light_mat,
            .Position        = position,
            .Range           = range,
            .Direction       = direction,
            .Color           = light.Color,
            .Intensity       = light.Intensity,
            .ConeInnerCutoff = std::cos( light.ConeInnerHalfAngle ),
            .ConeOuterCutoff = std::cos( light.ConeOuterHalfAngle ),
            .ShadowMap       = AllocateSpotShadow(),
        } );
      } );

  m_ShadowingLightCount = ( uint32_t )m_LightData.size();

  m_LightQuery.each(
      [&]( WorldTransform const& transform, SpotLight const& light )
      {
        DirectX::XMFLOAT3 const position = transform.GetTranslation();
        float const       range = light.Range > 0.0f ? light.Range : CalculateRange( light.Color, light.Intensity );
        DirectX::XMFLOAT3 direction;
        XMStoreFloat3(
            &direction,
            DirectX::XMVector3Normalize( DirectX::XMVector3Rotate(
                DirectX::XMVectorSet( 0.0f, 0.0f, -1.0f, 0.0f ),
                XMQuaternionRotationMatrix( transform.Transform ) ) ) );

        m_LightData.push_back( {
            .Position        = position,
            .Range           = range,
            .Direction       = direction,
            .Color           = light.Color,
            .Intensity       = light.Intensity,
            .ConeInnerCutoff = cos( light.ConeInnerHalfAngle ),
            .ConeOuterCutoff = cos( light.ConeOuterHalfAngle ),
            .ShadowMap       = {},
        } );
      } );

  m_TotalLightCount = ( uint32_t )m_LightData.size();

  if ( m_DataBuffers[frame_index].GetSize() < m_TotalLightCount * sizeof( SpotLightRepr ) )
  {
    m_DataBuffers[frame_index] =
        m_RenderDevice->CreateStorageBuffer( U32ByteSizeOf( m_LightData ), StrideOf( m_LightData ) );
    wchar_t name[] = L"Spot Light Buffer 0";
    name[18]       = L'0' + ( wchar_t )frame_index;
    m_DataBuffers[frame_index].SetName( name );
  }
  m_DataBuffers[frame_index].Write( 0, ByteSizeOf( m_LightData ), DataOf( m_LightData ) );

  return { m_DataBuffers[frame_index].GetSRVHandle(), m_ShadowingLightCount, m_TotalLightCount };
}

void Ember::Internal::SpotLightManager::RenderAllShadows(
    CommandList* command_list, DrawList::Batches const& draw_list, Camera const& camera, uint32_t const frame_idx )
{
  ZoneScoped;

  DirectX::BoundingFrustum const& camera_frustum = camera.GetLastUpdatedFrustum();

  command_list->SetGraphicsRootSignature( m_RootSignature.Get() );
  command_list->SetPipelineState( m_Pipeline.Get() );
  command_list->IASetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST );

  command_list->RSSetScissorViewport( kSpotShadowResolution, kSpotShadowResolution );

  static std::vector<D3D12_RESOURCE_BARRIER> barriers;
  barriers.resize( m_ShadowingLightCount );

  std::transform(
      m_ActiveShadows.begin(),
      m_ActiveShadows.begin() + m_ShadowingLightCount,
      barriers.begin(),
      []( Texture const& tex )
      {
        auto current_state = tex.GetCurrentState();
        auto next_state    = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        tex.SetCurrentState( next_state );
        return CD3DX12_RESOURCE_BARRIER::Transition( tex.GetTexture(), current_state, next_state );
      } );

  if ( not barriers.empty() ) command_list->ResourceBarrier( barriers );

  for ( uint32_t index = 0; index < m_ShadowingLightCount; index++ )
  {
    ZoneScopedN( "CheckSpotShadow" );
    ZoneValue( index );

    SpotLightRepr const&    light = m_LightData[index];

    DirectX::XMVECTOR const fwd   = XMLoadFloat3( &light.Direction );
    DirectX::XMVECTOR       up    = DirectX::XMVectorSet( 0.0f, 1.0f, 0.0f, 0.0f );
    DirectX::XMVECTOR const right = DirectX::XMVector3Normalize( DirectX::XMVector3Cross( fwd, up ) );
    up                            = DirectX::XMVector3Normalize( DirectX::XMVector3Cross( right, fwd ) );

    DirectX::XMFLOAT3 corners[5];
    corners[0] = light.Position;
    XMStoreFloat3(
        &corners[1],
        DirectX::XMVectorAdd(
            XMLoadFloat3( &light.Position ), DirectX::XMVectorAdd( DirectX::XMVectorScale( fwd, light.Range ), up ) ) );
    XMStoreFloat3(
        &corners[2],
        DirectX::XMVectorAdd(
            XMLoadFloat3( &light.Position ),
            DirectX::XMVectorSubtract( DirectX::XMVectorScale( fwd, light.Range ), up ) ) );
    XMStoreFloat3(
        &corners[3],
        DirectX::XMVectorAdd(
            XMLoadFloat3( &light.Position ),
            DirectX::XMVectorAdd( DirectX::XMVectorScale( fwd, light.Range ), right ) ) );
    XMStoreFloat3(
        &corners[4],
        DirectX::XMVectorAdd(
            XMLoadFloat3( &light.Position ),
            DirectX::XMVectorSubtract( DirectX::XMVectorScale( fwd, light.Range ), right ) ) );

    DirectX::BoundingOrientedBox bounding_box;
    DirectX::BoundingOrientedBox::CreateFromPoints(
        bounding_box, CountOf( corners ), DataOf( corners ), StrideOf( corners ) );

    if ( camera_frustum.Contains( bounding_box ) == DirectX::DISJOINT ) continue;

    RenderSpotShadow( command_list, draw_list, index, frame_idx );
  }

  std::transform(
      m_ActiveShadows.begin(),
      m_ActiveShadows.begin() + m_ShadowingLightCount,
      barriers.begin(),
      []( Texture const& tex )
      {
        auto current_state = tex.GetCurrentState();
        auto next_state    = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        tex.SetCurrentState( next_state );
        return CD3DX12_RESOURCE_BARRIER::Transition( tex.GetTexture(), current_state, next_state );
      } );

  if ( not barriers.empty() ) command_list->ResourceBarrier( barriers );
}

void Ember::Internal::SpotLightManager::RenderSpotShadow(
    CommandList*             command_list,
    DrawList::Batches const& draw_list,
    uint32_t const           spot_light_index,
    uint32_t const           frame_idx ) const
{
  PIXScopedEvent( command_list->Get(), PIX_COLOR_DEFAULT, "Render Spot Shadow %u", spot_light_index );
  ZoneScoped;

  auto& texture = m_ActiveShadows[spot_light_index];
  command_list->ClearDepthStencilView( texture.GetTexture(), D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0 );

  auto const       batch = DrawList::PerBatch::Opaque( draw_list.Unified );

  PackedData const packed_data{
    .LightBuffer = m_DataBuffers[frame_idx].GetSRVHandle(),
    .LightIndex  = spot_light_index,
  };

  command_list->SetGraphicsRootConstants( 0, batch );
  command_list->SetGraphicsRootConstants( 1, packed_data );

  command_list->OMSetRenderTargets( 0, nullptr, &texture );

  command_list->DispatchMesh( { .X = batch.CommandsCount } );
}
