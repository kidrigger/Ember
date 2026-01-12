#include "OmniLightManager.hpp"

#include <Util/DataUtil.hpp>
#include <Util/Profiling.hpp>
#include "Camera.hpp"
#include "ModelLoader.hpp"

namespace
{
struct PackedData
{
  DirectX::XMFLOAT3 Position;
  float             FarPlane;
  Ember::CBVHandle  ProjViewHandle;
};

static_assert( sizeof( PackedData ) == 20 );
} // namespace

Ember::Internal::OmniLightManager::OmniLightManager(
    RenderDevice*               render_device,
    World*                      world,
    Buffer                      projection_buffer,
    std::vector<Buffer>         light_buffers,
    ComPtr<ID3D12PipelineState> shadow_pipeline,
    ComPtr<ID3D12RootSignature> shadow_root_signature )
  : m_RenderDevice{ render_device }
  , m_World{ world }
  , m_ShadowProjectionBuffer{ std::move( projection_buffer ) }
  , m_RootSignature{ std::move( shadow_root_signature ) }
  , m_Pipeline{ std::move( shadow_pipeline ) }
  , m_DataBuffers{ std::move( light_buffers ) }
{
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

  m_LightQuery =
      m_World->GetECS().query_builder<WorldTransform const, OmniLight const>().without<ShadowCaster>().build();

  m_ShadowLightQuery =
      m_World->GetECS().query_builder<WorldTransform const, OmniLight const>().with<ShadowCaster>().build();
}

void Ember::Internal::OmniLightManager::Create(
    OmniLightManager* light_manager, RenderDevice* render_device, World* world, uint32_t const num_frames )
{

  Buffer              proj_buffer = render_device->CreateConstantBuffer( 6 * sizeof( DirectX::XMMATRIX ) );

  std::vector<Buffer> buffers;
  buffers.reserve( num_frames );
  for ( uint32_t i = 0; i < num_frames; i++ )
  {
    buffers.push_back( render_device->CreateStorageBuffer( 8 * sizeof( OmniLightRepr ), sizeof( OmniLightRepr ) ) );
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
    world,
    std::move( proj_buffer ),
    std::move( buffers ),
    std::move( shadow_pipeline ),
    std::move( shadow_root_sig ),
  };
}

float Ember::Internal::OmniLightManager::CalculateRange( Color32 const color, float const intensity )
{
  auto [x, y, z]       = color.UnpackRgb();
  float const max_comp = std::max( x, std::max( y, z ) );
  return sqrt( ( max_comp * intensity ) ) * 10.0f;
}

Ember::SRVHandle Ember::Internal::OmniLightManager::AllocateOmniShadow()
{
  if ( m_AllocatedShadows == m_ActiveShadows.size() )
  {
    m_ActiveShadows.emplace_back( m_RenderDevice->CreateTextureCube( {
        .Format    = DXGI_FORMAT_D16_UNORM,
        .Side      = kOmniShadowResolution,
        .Usage     = TextureUsage::kDepthStencil,
        .MipLevels = MipLevels::kBase,
        .InitState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
    } ) );

    wchar_t buf[32];
    swprintf_s( buf, L"Omni Shadow Map %u", m_AllocatedShadows );
    m_ActiveShadows.back().SetName( buf );
  }

  return m_ActiveShadows[m_AllocatedShadows++].GetSRVHandle();
}

void Ember::Internal::OmniLightManager::ClearShadows()
{
  m_AllocatedShadows = 0;
}

Ember::LightInfo Ember::Internal::OmniLightManager::PrepareFrame( uint32_t const frame_index )
{
  m_LightData.clear();
  ClearShadows();

  m_ShadowLightQuery.each(
      [&]( WorldTransform const& transform, OmniLight const& light )
      {
        DirectX::XMFLOAT3 const position = transform.GetTranslation();
        float const range = light.Range > 0.0f ? light.Range : CalculateRange( light.Color, light.Intensity );

        m_LightData.push_back( {
            .Position    = position,
            .Range       = range,
            .Color       = light.Color,
            .Intensity   = light.Intensity,
            .Attenuation = 1.0f,
            .ShadowMap   = AllocateOmniShadow(),
        } );
      } );

  m_ShadowingLightCount = ( uint32_t )m_LightData.size();

  m_LightQuery.each(
      [&]( WorldTransform const& transform, OmniLight const& light )
      {
        DirectX::XMFLOAT3 const position = transform.GetTranslation();
        float const range = light.Range > 0.0f ? light.Range : CalculateRange( light.Color, light.Intensity );

        m_LightData.push_back( {
            .Position    = position,
            .Range       = range,
            .Color       = light.Color,
            .Intensity   = light.Intensity,
            .Attenuation = 1.0f,
        } );
      } );

  m_TotalLightCount = ( uint32_t )m_LightData.size();

  if ( m_DataBuffers[frame_index].GetSize() < m_TotalLightCount * sizeof( OmniLightRepr ) )
  {
    m_DataBuffers[frame_index] =
        m_RenderDevice->CreateStorageBuffer( U32ByteSizeOf( m_LightData ), StrideOf( m_LightData ) );
    wchar_t name[] = L"Omni Light Buffer 0";
    name[18]       = L'0' + ( wchar_t )frame_index;
    m_DataBuffers[frame_index].SetName( name );
  }
  m_DataBuffers[frame_index].Write( 0, ByteSizeOf( m_LightData ), DataOf( m_LightData ) );

  return { m_DataBuffers[frame_index].GetSRVHandle(), m_ShadowingLightCount, m_TotalLightCount };
}

void Ember::Internal::OmniLightManager::RenderAllShadows(
    CommandList* command_list, DrawList::Batches const& draw_list, Camera const& camera )
{
  ZoneScoped;

  DirectX::BoundingFrustum const& camera_frustum = camera.GetLastUpdatedFrustum();

  command_list->SetGraphicsRootSignature( m_RootSignature.Get() );
  command_list->SetPipelineState( m_Pipeline.Get() );
  command_list->IASetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST );

  command_list->RSSetScissorViewport( kOmniShadowResolution, kOmniShadowResolution );

  static std::vector<D3D12_RESOURCE_BARRIER> barriers;
  barriers.resize( m_AllocatedShadows );

  std::transform(
      m_ActiveShadows.begin(),
      m_ActiveShadows.begin() + m_AllocatedShadows,
      barriers.begin(),
      []( Texture const& tex )
      {
        auto current_state = tex.GetCurrentState();
        auto next_state    = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        tex.SetCurrentState( next_state );
        return CD3DX12_RESOURCE_BARRIER::Transition( tex.GetTexture(), current_state, next_state );
      } );

  if ( not barriers.empty() ) command_list->ResourceBarrier( barriers );

  for ( uint32_t index = 0; index < m_AllocatedShadows; ++index )
  {
    ZoneScopedN( "CheckOmniShadow" );
    ZoneValue( index );

    OmniLightRepr const&    light = m_LightData[index];

    DirectX::BoundingSphere sphere_of_influence{ light.Position, light.Range };
    if ( camera_frustum.Contains( sphere_of_influence ) == DirectX::DISJOINT ) continue;

    RenderOmniShadow( command_list, draw_list, index );
  }

  std::transform(
      m_ActiveShadows.begin(),
      m_ActiveShadows.begin() + m_AllocatedShadows,
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

void Ember::Internal::OmniLightManager::RenderOmniShadow(
    CommandList* command_list, DrawList::Batches const& draw_list, uint32_t const light_index ) const
{
  PIXScopedEvent( command_list->Get(), PIX_COLOR_DEFAULT, "Render Omni Shadow %u", light_index );
  ZoneScoped;

  OmniLightRepr const& omni_light = m_LightData[light_index];
  Texture const&       texture    = m_ActiveShadows[light_index];

  command_list->ClearDepthStencilView( texture.GetTexture(), D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0 );

  auto const       batch = draw_list.Opaque();

  PackedData const packed_data{
    .Position       = omni_light.Position,
    .FarPlane       = omni_light.Range,
    .ProjViewHandle = m_ShadowProjectionBuffer.GetCBVHandle(),
  };
  command_list->SetGraphicsRootConstants( 0, batch );
  command_list->SetGraphicsRootConstants( 1, packed_data );

  command_list->OMSetRenderTargets( 0, nullptr, &texture );

  command_list->DispatchMesh( { .X = batch.CommandsCount } );
}
