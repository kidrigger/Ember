#include "TextureLoader.hpp"

#include <Util/HelperUtils.hpp>

#include <span>

#include <Graphics/RenderDevice.hpp>
#include <Util/DataUtil.hpp>
#include <Util/DirectXHeaders.hpp>
#include <Util/Profiling.hpp>

namespace
{
uint32_t constexpr kMaxMipCount = 14;
}

struct MipMapRootSigInfo
{
  DirectX::XMFLOAT2 TexelSize;
  Ember::SRVHandle  Src;
  Ember::UAVHandle  Dst;
  uint32_t          SrcMipLevel;
  uint32_t          IsSrgb;
};

Ember::TextureLoader::UploadBatch::UploadBatch(
    RenderDevice* render_device, Context::Receipt receipt, std::pmr::polymorphic_allocator<> const& pool_allocator )
  : Tracker{ render_device, pool_allocator }, Receipt{ std::move( receipt ) }
{}

void Ember::TextureLoader::UploadBatch::PushUpload(
    ComPtr<ID3D12Resource> dest, ComPtr<IUnknown> intermediate, D3D12_RESOURCE_STATES const final_state )
{
  Tracker.PushBarrier(
      CD3DX12_RESOURCE_BARRIER::Transition( dest.Get(), D3D12_RESOURCE_STATE_COPY_DEST, final_state ) );
  Tracker.PushResource( std::move( dest ) );
  Tracker.PushResource( std::move( intermediate ) );
}

void Ember::TextureLoader::UploadBatch::PushAllocation( ComPtr<D3D12MA::Allocation> intermediate )
{
  Tracker.PushResource( std::move( intermediate ) );
}

void Ember::TextureLoader::UploadBatch::PushAlias( ComPtr<ID3D12Resource> alias )
{
  Tracker.PushResource( std::move( alias ) );
}

void Ember::TextureLoader::UploadBatch::PushHandle( SRVHandle const handle )
{
  Tracker.PushHandle( handle );
}

void Ember::TextureLoader::UploadBatch::PushHandles( std::span<UAVHandle> const& handles )
{
  for ( auto const handle : handles )
  {
    Tracker.PushHandle( handle );
  }
}

void Ember::TextureLoader::UploadBatch::ClearResources( std::vector<D3D12_RESOURCE_BARRIER>* barriers )
{
  Tracker.Clear( barriers );
}

DXGI_FORMAT MakeUAVCompat( DXGI_FORMAT const format )
{
  switch ( format )
  {
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
      return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R32_FLOAT:
    case DXGI_FORMAT_R32G32_FLOAT:
    case DXGI_FORMAT_R32G32B32_FLOAT:
    case DXGI_FORMAT_R32G32B32A32_FLOAT:
    case DXGI_FORMAT_R11G11B10_FLOAT:
    case DXGI_FORMAT_R8_UNORM:
    case DXGI_FORMAT_R16G16B16A16_UNORM:
      return format;
    default:
      UNIMPLEMENTED_M( "Add formats as used/required" );
  }
}

// Thread unsafe
bool Ember::TextureLoader::TryGenerateMipMaps(
    ID3D12GraphicsCommandList* command_list, Texture* texture, ResourceTracker* tracker ) const
{
  ComPtr<ID3D12Resource> uav_capable;

  // Grab the 'necessary' info from texture, and copy it to the 'alias' resource.
#if not defined( RENDERDOC_COMPAT )
  D3D12MA::Allocation* allocation = texture->GetAllocation();
  ID3D12Resource*      resource   = allocation->GetResource();
#else
  ID3D12Resource* resource = texture->GetTexture();
#endif

  D3D12_RESOURCE_DESC desc      = resource->GetDesc();
  DXGI_FORMAT         in_format = desc.Format;

  desc.Flags  &= ~( D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL );
  desc.Flags  |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  desc.Format  = MakeUAVCompat( desc.Format );

#if not defined( RENDERDOC_COMPAT )
  bool const is_aliased = allocation->GetHeap() != nullptr;
  if ( is_aliased )
  {
    // Placed resource can be aliased.
    ERR_FAIL_RET_V(
        m_RenderDevice->GetAllocator()->CreateAliasingResource(
            allocation, 0, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS( &uav_capable ) ),
        false );
  }
  else
  {
    D3D12MA::ALLOCATION_DESC allocation_desc{
      .Flags    = D3D12MA::ALLOCATION_FLAG_CAN_ALIAS,
      .HeapType = D3D12_HEAP_TYPE_DEFAULT,
    };
    ComPtr<D3D12MA::Allocation> copy_alloc;
    ERR_FAIL_RET_V(
        m_RenderDevice->GetAllocator()->CreateResource(
            &allocation_desc,
            &desc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            &copy_alloc,
            IID_PPV_ARGS( &uav_capable ) ),
        false );
    tracker->PushResource( std::move( copy_alloc ) );
  }
#else
  CD3DX12_HEAP_PROPERTIES properties{ D3D12_HEAP_TYPE_DEFAULT };
  ERR_FAIL_RET_V(
      m_RenderDevice->GetDevice()->CreateCommittedResource(
          &properties,
          D3D12_HEAP_FLAG_NONE,
          &desc,
          D3D12_RESOURCE_STATE_COPY_DEST,
          nullptr,
          IID_PPV_ARGS( &uav_capable ) ),
      false );
#endif

  tracker->PushResource( uav_capable );

  ERR_FAIL_RET_V( uav_capable->SetName( L"UAV Alias" ), false );
  {
    CD3DX12_RESOURCE_BARRIER const transition = CD3DX12_RESOURCE_BARRIER::Transition(
        resource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE );
    command_list->ResourceBarrier( 1, &transition );
  }

#if not defined( RENDERDOC_COMPAT )
  if ( is_aliased )
  {
    CD3DX12_RESOURCE_BARRIER aliasing_barrier =
        CD3DX12_RESOURCE_BARRIER::Aliasing( allocation->GetResource(), uav_capable.Get() );
    command_list->ResourceBarrier( 1, &aliasing_barrier );
  }
#endif

  command_list->CopyResource( uav_capable.Get(), resource );

  SRVHandle mip_src_handle;
  UAVHandle mip_dst_handles[kMaxMipCount];

  {
    CD3DX12_SHADER_RESOURCE_VIEW_DESC srv_desc = CD3DX12_SHADER_RESOURCE_VIEW_DESC::Tex2D( in_format );
    mip_src_handle                             = m_RenderDevice->CreateBindlessHandle( uav_capable.Get(), srv_desc );
  }

  tracker->PushHandle( mip_src_handle );

  for ( int level = 0; level < desc.MipLevels; level++ )
  {
    CD3DX12_UNORDERED_ACCESS_VIEW_DESC uav_desc = CD3DX12_UNORDERED_ACCESS_VIEW_DESC::Tex2D( desc.Format, level );
    UAVHandle                          handle   = m_RenderDevice->CreateBindlessHandle( uav_capable.Get(), uav_desc );
    mip_dst_handles[level]                      = handle;
    tracker->PushHandle( handle );
  }

  uint32_t constexpr static kThreadGroupX      = 8;
  uint32_t constexpr static kThreadGroupY      = 8;
  uint32_t constexpr static kThreadGroupZ      = 1;

  uint32_t                 tex_width           = ( UINT )desc.Width;
  uint32_t                 tex_height          = desc.Height;

  CD3DX12_RESOURCE_BARRIER pre_compute_barrier = CD3DX12_RESOURCE_BARRIER::Transition(
      uav_capable.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS );

  command_list->ResourceBarrier( 1, &pre_compute_barrier );

  auto bindless_desc_heaps = m_RenderDevice->GetBindlessDescriptorHeaps();

  command_list->SetDescriptorHeaps( CountOf( bindless_desc_heaps ), DataOf( bindless_desc_heaps ) );
  command_list->SetPipelineState( m_MipmapPipeline.Get() );
  command_list->SetComputeRootSignature( m_MipMapRootSig.Get() );

  MipMapRootSigInfo mip_map_info;
  mip_map_info.Src                           = mip_src_handle;
  mip_map_info.IsSrgb                        = DirectX::IsSRGB( in_format );

  CD3DX12_RESOURCE_BARRIER inter_mip_barrier = CD3DX12_RESOURCE_BARRIER::UAV( uav_capable.Get() );
  for ( int write_lvl = 1; write_lvl < desc.MipLevels; write_lvl++ )
  {
    tex_width                 = std::max( tex_width / 2, 1u );
    tex_height                = std::max( tex_height / 2, 1u );

    mip_map_info.TexelSize    = { 1.0f / ( float )tex_width, 1.0f / ( float )tex_height };
    mip_map_info.Dst          = mip_dst_handles[write_lvl];
    mip_map_info.SrcMipLevel  = write_lvl - 1;

    uint32_t const dispatch_x = std::max( 1u, tex_width / kThreadGroupX );
    uint32_t const dispatch_y = std::max( 1u, tex_height / kThreadGroupY );
    uint32_t const dispatch_z = std::max( 1u, 1 / kThreadGroupZ );

    command_list->SetComputeRoot32BitConstants( 0, sizeof( MipMapRootSigInfo ) / 4, &mip_map_info, 0 );
    command_list->Dispatch( dispatch_x, dispatch_y, dispatch_z );
    command_list->ResourceBarrier( 1, &inter_mip_barrier );
  }

  {
    CD3DX12_RESOURCE_BARRIER barriers[] = {
      CD3DX12_RESOURCE_BARRIER::Transition(
          uav_capable.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE ),
      CD3DX12_RESOURCE_BARRIER::Transition(
          resource, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST ),
    };
    command_list->ResourceBarrier( CountOf( barriers ), DataOf( barriers ) );
  }
  command_list->CopyResource( resource, uav_capable.Get() );

#if not defined( RENDERDOC_COMPAT )
  if ( is_aliased )
  {
    CD3DX12_RESOURCE_BARRIER reverse_aliasing = CD3DX12_RESOURCE_BARRIER::Aliasing( uav_capable.Get(), resource );
    command_list->ResourceBarrier( 1, &reverse_aliasing );
  }
#endif

  return true;
}

bool Ember::TextureLoader::TryGenerateMipMapCube(
    ID3D12GraphicsCommandList* command_list,
    Texture*                   texture,
    ResourceTracker*           tracker,
    D3D12_RESOURCE_STATES      texture_resource_state ) const
{
  ComPtr<ID3D12Resource> uav_capable;

  // Grab the 'necessary' info from texture, and copy it to the 'alias' resource.
#if not defined( RENDERDOC_COMPAT )
  D3D12MA::Allocation* allocation = texture->GetAllocation();
  ID3D12Resource*      resource   = allocation->GetResource();
#else
  ID3D12Resource* resource = texture->GetTexture();
#endif

  D3D12_RESOURCE_DESC desc      = resource->GetDesc();
  DXGI_FORMAT         in_format = desc.Format;

  desc.Flags  &= ~( D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL );
  desc.Flags  |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  desc.Format  = MakeUAVCompat( desc.Format );

#if not defined( RENDERDOC_COMPAT )
  bool const is_aliased = allocation->GetHeap() != nullptr;
  if ( is_aliased )
  {
    // Placed resource can be aliased.
    ERR_FAIL_RET_V(
        m_RenderDevice->GetAllocator()->CreateAliasingResource(
            allocation, 0, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS( &uav_capable ) ),
        false );
  }
  else
  {
    D3D12MA::ALLOCATION_DESC allocation_desc{
      .Flags    = D3D12MA::ALLOCATION_FLAG_CAN_ALIAS,
      .HeapType = D3D12_HEAP_TYPE_DEFAULT,
    };
    ComPtr<D3D12MA::Allocation> copy_alloc;
    ERR_FAIL_RET_V(
        m_RenderDevice->GetAllocator()->CreateResource(
            &allocation_desc,
            &desc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            &copy_alloc,
            IID_PPV_ARGS( &uav_capable ) ),
        false );
    tracker->PushResource( std::move( copy_alloc ) );
  }
#else
  CD3DX12_HEAP_PROPERTIES properties{ D3D12_HEAP_TYPE_DEFAULT };
  ERR_FAIL_RET_V(
      m_RenderDevice->GetDevice()->CreateCommittedResource(
          &properties,
          D3D12_HEAP_FLAG_NONE,
          &desc,
          D3D12_RESOURCE_STATE_COPY_DEST,
          nullptr,
          IID_PPV_ARGS( &uav_capable ) ),
      false );
#endif

  tracker->PushResource( uav_capable );

  ERR_FAIL_RET_V( uav_capable->SetName( L"UAV Alias" ), false );
  {
    CD3DX12_RESOURCE_BARRIER const transition =
        CD3DX12_RESOURCE_BARRIER::Transition( resource, texture_resource_state, D3D12_RESOURCE_STATE_COPY_SOURCE );
    command_list->ResourceBarrier( 1, &transition );
  }

#if not defined( RENDERDOC_COMPAT )
  if ( is_aliased )
  {
    CD3DX12_RESOURCE_BARRIER aliasing_barrier =
        CD3DX12_RESOURCE_BARRIER::Aliasing( allocation->GetResource(), uav_capable.Get() );
    command_list->ResourceBarrier( 1, &aliasing_barrier );
  }
#endif

  command_list->CopyResource( uav_capable.Get(), resource );

  SRVHandle mip_src_handle;
  UAVHandle mip_dst_handles[kMaxMipCount];

  {
    auto srv_desc  = CD3DX12_SHADER_RESOURCE_VIEW_DESC::TexCube( in_format );
    mip_src_handle = m_RenderDevice->CreateBindlessHandle( uav_capable.Get(), srv_desc );
  }

  tracker->PushHandle( mip_src_handle );

  for ( int level = 0; level < desc.MipLevels; level++ )
  {
    auto      uav_desc     = CD3DX12_UNORDERED_ACCESS_VIEW_DESC::Tex2DArray( desc.Format, 6, 0, level );
    UAVHandle handle       = m_RenderDevice->CreateBindlessHandle( uav_capable.Get(), uav_desc );
    mip_dst_handles[level] = handle;
    tracker->PushHandle( handle );
  }

  uint32_t constexpr static kThreadGroupX      = 8;
  uint32_t constexpr static kThreadGroupY      = 8;
  uint32_t constexpr static kThreadGroupZ      = 1;

  uint32_t                 tex_width           = ( UINT )desc.Width;
  uint32_t                 tex_height          = desc.Height;

  CD3DX12_RESOURCE_BARRIER pre_compute_barrier = CD3DX12_RESOURCE_BARRIER::Transition(
      uav_capable.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS );

  command_list->ResourceBarrier( 1, &pre_compute_barrier );

  auto bindless_desc_heaps = m_RenderDevice->GetBindlessDescriptorHeaps();

  command_list->SetPipelineState( m_MipmapCubePipeline.Get() );
  command_list->SetComputeRootSignature( m_MipMapRootSig.Get() );
  command_list->SetDescriptorHeaps( CountOf( bindless_desc_heaps ), DataOf( bindless_desc_heaps ) );

  MipMapRootSigInfo mip_map_info;
  mip_map_info.Src                           = mip_src_handle;
  mip_map_info.IsSrgb                        = DirectX::IsSRGB( in_format );

  CD3DX12_RESOURCE_BARRIER inter_mip_barrier = CD3DX12_RESOURCE_BARRIER::UAV( uav_capable.Get() );
  for ( int write_lvl = 1; write_lvl < desc.MipLevels; write_lvl++ )
  {
    tex_width                 = std::max( tex_width / 2, 1u );
    tex_height                = std::max( tex_height / 2, 1u );

    mip_map_info.TexelSize    = { 1.0f / ( float )tex_width, 1.0f / ( float )tex_height };
    mip_map_info.Dst          = mip_dst_handles[write_lvl];
    mip_map_info.SrcMipLevel  = write_lvl - 1;

    uint32_t const dispatch_x = std::max( 1u, tex_width / kThreadGroupX );
    uint32_t const dispatch_y = std::max( 1u, tex_height / kThreadGroupY );
    uint32_t const dispatch_z = std::max( 1u, 6 / kThreadGroupZ );

    command_list->SetComputeRoot32BitConstants( 0, sizeof( MipMapRootSigInfo ) / 4, &mip_map_info, 0 );
    command_list->Dispatch( dispatch_x, dispatch_y, dispatch_z );
    command_list->ResourceBarrier( 1, &inter_mip_barrier );
  }

  {
    CD3DX12_RESOURCE_BARRIER barriers[] = {
      CD3DX12_RESOURCE_BARRIER::Transition(
          uav_capable.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE ),
      CD3DX12_RESOURCE_BARRIER::Transition(
          resource, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST ),
    };
    command_list->ResourceBarrier( CountOf( barriers ), DataOf( barriers ) );
  }
  command_list->CopyResource( resource, uav_capable.Get() );

#if not defined( RENDERDOC_COMPAT )
  if ( is_aliased )
  {
    CD3DX12_RESOURCE_BARRIER reverse_aliasing = CD3DX12_RESOURCE_BARRIER::Aliasing( uav_capable.Get(), resource );
    command_list->ResourceBarrier( 1, &reverse_aliasing );
  }
#endif

  CD3DX12_RESOURCE_BARRIER barrier =
      CD3DX12_RESOURCE_BARRIER::Transition( resource, D3D12_RESOURCE_STATE_COPY_DEST, texture_resource_state );
  command_list->ResourceBarrier( 1, &barrier );

  return true;
}

Ember::TextureLoader::TextureLoader(
    RenderDevice*               render_device,
    ComPtr<ID3D12RootSignature> mipmap_root_signature,
    ComPtr<ID3D12PipelineState> mipmap_pipeline,
    ComPtr<ID3D12PipelineState> mipmap_cube_pipeline,
    Context                     copy_context,
    uint32_t const              upload_frame_count )
  : m_RenderDevice{ render_device }
  , m_CopyContext{ std::move( copy_context ) }
  , m_MipMapRootSig{ std::move( mipmap_root_signature ) }
  , m_MipmapPipeline{ std::move( mipmap_pipeline ) }
  , m_MipmapCubePipeline{ std::move( mipmap_cube_pipeline ) }
{
  m_UploadBatches.reserve( upload_frame_count );
  Context::Receipt initial = m_CopyContext.CreateReceipt();
  for ( int i = 0; i < ( int )upload_frame_count; ++i )
  {
    m_UploadBatches.emplace_back( m_RenderDevice, initial, &m_InFlightPool );
  }
  m_CurrentCommandList = m_CopyContext.GetCommandList();
}

void Ember::TextureLoader::Create( TextureLoader* loader, RenderDevice* render_device, uint32_t upload_frame_count )
{
  ComPtr<ID3D12Device2> device = render_device->GetDevice();

  // We need COMPUTE instead of COPY due to the mip-mapping.
  // Ideally, we want to kick the job to an async compute queue.
  Context transfer_context;
  Context::Create( &transfer_context, device, D3D12_COMMAND_LIST_TYPE_COMPUTE );

  ComPtr<ID3DBlob> mipmap_shader;
  ERR_ABORT( D3DReadFileToBlob( L"MipMap.cso", &mipmap_shader ) );
  ComPtr<ID3DBlob> mipmap_cube_shader;
  ERR_ABORT( D3DReadFileToBlob( L"MipMapCube.cso", &mipmap_cube_shader ) );

  D3D_ROOT_SIGNATURE_VERSION  highest_root_signature_version = render_device->FetchHighestRootSignatureVersion();

  CD3DX12_STATIC_SAMPLER_DESC static_sampler_desc;
  static_sampler_desc.Init(
      0,
      D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR,
      D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
      D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
      D3D12_TEXTURE_ADDRESS_MODE_CLAMP );

  D3D12_ROOT_SIGNATURE_FLAGS const root_signature_flags =
      D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
      D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED |
      D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED | D3D12_ROOT_SIGNATURE_FLAG_DENY_MESH_SHADER_ROOT_ACCESS |
      D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS |
      D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS |
      D3D12_ROOT_SIGNATURE_FLAG_DENY_AMPLIFICATION_SHADER_ROOT_ACCESS |
      D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
      D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS | D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS;

  CD3DX12_ROOT_PARAMETER1 root_parameters[1];
  root_parameters[0].InitAsConstants( sizeof( MipMapRootSigInfo ) / 4, 0 );

  CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC root_signature_desc;
  root_signature_desc.Init_1_1(
      CountOf( root_parameters ), DataOf( root_parameters ), 1, &static_sampler_desc, root_signature_flags );

  ComPtr<ID3DBlob> root_signature_blob;
  ComPtr<ID3DBlob> error_blob;
  ERR_ABORT( D3DX12SerializeVersionedRootSignature(
      &root_signature_desc, highest_root_signature_version, &root_signature_blob, &error_blob ) );

  ComPtr<ID3D12RootSignature> root_signature;
  ERR_ABORT( device->CreateRootSignature(
      0,
      root_signature_blob->GetBufferPointer(),
      root_signature_blob->GetBufferSize(),
      IID_PPV_ARGS( &root_signature ) ) );

  struct PipelineStateStream
  {
    CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE RootSignature;
    CD3DX12_PIPELINE_STATE_STREAM_CS             CS;
  };
  PipelineStateStream pipeline_stream = {
    .RootSignature = root_signature.Get(),
  };

  D3D12_PIPELINE_STATE_STREAM_DESC const pipeline_state_stream_desc = {
    .SizeInBytes                   = sizeof pipeline_stream,
    .pPipelineStateSubobjectStream = &pipeline_stream,
  };

  pipeline_stream.CS = CD3DX12_SHADER_BYTECODE( mipmap_shader.Get() );
  ComPtr<ID3D12PipelineState> mipmap_pipeline;
  ERR_ABORT( device->CreatePipelineState( &pipeline_state_stream_desc, IID_PPV_ARGS( &mipmap_pipeline ) ) );

  pipeline_stream.CS = CD3DX12_SHADER_BYTECODE( mipmap_cube_shader.Get() );
  ComPtr<ID3D12PipelineState> mipmap_cube_pipeline;
  ERR_ABORT( device->CreatePipelineState( &pipeline_state_stream_desc, IID_PPV_ARGS( &mipmap_cube_pipeline ) ) );

  new ( loader ) TextureLoader{
    render_device,
    std::move( root_signature ),
    std::move( mipmap_pipeline ),
    std::move( mipmap_cube_pipeline ),
    std::move( transfer_context ),
    upload_frame_count,
  };
}

bool Ember::TextureLoader::TryLoadImpl(
    Texture*                     texture,
    char const*                  id,
    DirectX::TexMetadata const&  metadata,
    DirectX::ScratchImage const& scratch_image,
    ColorSpaceOverride const     color_space_override,
    D3D12_RESOURCE_STATES        final_state )
{
  PIXScopedEvent( m_CurrentCommandList.Get(), PIX_COLOR_DEFAULT, "TextureLoader::TryLoadImpl %s", id );

  std::span const images{ scratch_image.GetImages(), scratch_image.GetImageCount() };

  DXGI_FORMAT     format = metadata.format;
  switch ( color_space_override )
  {
    case ColorSpaceOverride::kLinear:
      format = DirectX::MakeLinear( format );
      break;
    case ColorSpaceOverride::kSrgb:
      format = DirectX::MakeSRGB( format );
      break;
    case ColorSpaceOverride::kNone:
      break;
  }

  new ( texture )
      Texture{ m_RenderDevice->CreateTexture2D( { format, ( uint32_t )metadata.width, ( uint32_t )metadata.height } ) };

  wchar_t wide_id[512];
  MultiByteToWideChar( CP_UTF8, MB_ERR_INVALID_CHARS, id, -1, wide_id, 512 );
  ERR_FAIL_RET_V( texture->GetTexture()->SetName( wide_id ), false );

  uint64_t const              req_size = GetRequiredIntermediateSize( texture->GetTexture(), 0, 1 );

  ComPtr<ID3D12Resource>      staging_res;
  ComPtr<D3D12MA::Allocation> staging_alloc;
  {
    CD3DX12_RESOURCE_DESC const resource_desc = CD3DX12_RESOURCE_DESC::Buffer( req_size );

#if not defined( RENDERDOC_COMPAT )
    D3D12MA::ALLOCATION_DESC const allocation_desc = {
      .Flags    = D3D12MA::ALLOCATION_FLAG_NONE,
      .HeapType = D3D12_HEAP_TYPE_UPLOAD,
    };

    ERR_FAIL_RET_V(
        m_RenderDevice->GetAllocator()->CreateResource(
            &allocation_desc,
            &resource_desc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            staging_alloc.GetAddressOf(),
            IID_PPV_ARGS( &staging_res ) ),
        false );
#else
    auto heap_property = CD3DX12_HEAP_PROPERTIES{ D3D12_HEAP_TYPE_UPLOAD };
    ERR_FAIL_RET_V(
        m_RenderDevice->GetDevice()->CreateCommittedResource(
            &heap_property,
            D3D12_HEAP_FLAG_NONE,
            &resource_desc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS( &staging_res ) ),
        false );
#endif
  }
  wchar_t staging_name[512];
  swprintf_s( staging_name, L"Staging: %s", wide_id );
  ERR_FAIL_RET_V( staging_res->SetName( staging_name ), false );

  auto lock_guard = std::lock_guard( m_LoadLock );
  // [1] Needs to run on a single thread from here.

  static std::vector<D3D12_SUBRESOURCE_DATA> subresources;
  subresources.clear(); // Doesn't release resource, so we get to keep reusing allocation.
  for ( auto& image : images )
  {
    subresources.push_back( {
        .pData      = image.pixels,
        .RowPitch   = ( LONG_PTR )image.rowPitch,
        .SlicePitch = ( LONG_PTR )image.slicePitch,
    } );
  }

  ++m_CurrentUploadBatchSize;

  UpdateSubresources(
      m_CurrentCommandList.Get(),
      texture->GetTexture(),
      staging_res.Get(),
      0,
      0,
      CountOf( subresources ),
      DataOf( subresources ) );

#if not defined( RENDERDOC_COMPAT )
  m_UploadBatches[m_CurrentUploadBatch].PushUpload( texture->GetTexture(), staging_alloc, final_state );
#else
  m_UploadBatches[m_CurrentUploadBatch].PushUpload( texture->GetTexture(), staging_res, final_state );
#endif

  if ( not TryGenerateMipMaps( m_CurrentCommandList.Get(), texture, &m_UploadBatches[m_CurrentUploadBatch].Tracker ) )
  {
    return false;
  }

  m_Cache[id] = *texture;

  return true;
}

bool Ember::TextureLoader::TryLoadTexture(
    Texture*                    texture,
    char const*                 filename,
    ColorSpaceOverride const    color_space_override,
    D3D12_RESOURCE_STATES const final_state )
{
  auto const it = m_Cache.find( filename );
  if ( it != m_Cache.end() )
  {
    new ( texture ) Texture{ it->second };
  }

  std::filesystem::path const file_path( filename );
  if ( not exists( file_path ) ) return false;
  if ( not file_path.has_extension() ) return false;

  wchar_t wide_filename[512];
  MultiByteToWideChar( CP_UTF8, MB_ERR_INVALID_CHARS, filename, -1, wide_filename, 512 );

  DirectX::TexMetadata  metadata;
  DirectX::ScratchImage scratch_image;

  if ( file_path.extension() == ".hdr" )
  {
    ERR_FAIL_RET_F( DirectX::LoadFromHDRFile( wide_filename, &metadata, scratch_image ) );
  }
  else
  {
    // TODO: This is dicey. Might wanna support "R" format etc.
    DirectX::WIC_FLAGS const flags = DirectX::WIC_FLAGS_DEFAULT_SRGB | DirectX::WIC_FLAGS_FORCE_RGB;
    ERR_FAIL_RET_F( LoadFromWICFile( wide_filename, flags, &metadata, scratch_image ) );
  }

  return TryLoadImpl( texture, filename, metadata, scratch_image, color_space_override, final_state );
}

bool Ember::TextureLoader::TryLoadTextureFromData(
    Texture*                    texture,
    char const*                 id,
    size_t const                data_size,
    byte const*                 data,
    ColorSpaceOverride const    color_space_override,
    D3D12_RESOURCE_STATES const final_state )
{
  auto const it = m_Cache.find( id );
  if ( it != m_Cache.end() )
  {
    new ( texture ) Texture{ it->second };
    return true;
  }

  DirectX::TexMetadata     metadata;
  DirectX::ScratchImage    scratch_image;
  DirectX::WIC_FLAGS const flags = DirectX::WIC_FLAGS_DEFAULT_SRGB | DirectX::WIC_FLAGS_FORCE_RGB;
  ERR_FAIL_RET_V( DirectX::LoadFromWICMemory( data, data_size, flags, &metadata, scratch_image ), false );

  return TryLoadImpl( texture, id, metadata, scratch_image, color_space_override, final_state );
}

Ember::Context::Receipt Ember::TextureLoader::EndBatch()
{
  m_UploadBatches[m_CurrentUploadBatch].Receipt = m_CopyContext.Submit( std::move( m_CurrentCommandList ) );

  Context::Receipt const batch_receipt          = m_UploadBatches[m_CurrentUploadBatch].Receipt;

  m_CurrentUploadBatch++;
  m_CurrentUploadBatch %= m_UploadBatches.size();

  m_CopyContext.WaitOn( m_UploadBatches[m_CurrentUploadBatch].Receipt );

  auto lock_guard = std::lock_guard( m_LoadLock );

  m_UploadBatches[m_CurrentUploadBatch].ClearResources( &m_PendingBarriers );

  m_CurrentCommandList     = m_CopyContext.GetCommandList();
  m_CurrentUploadBatchSize = 0;

  return batch_receipt;
}

void Ember::TextureLoader::Update()
{
  // TODO: Ideally, I don't want this as a check on 'Update' but instead it should be a task on a thread pool.
  for ( auto& batch : m_UploadBatches )
  {
    ASSERT_M(
        batch.Tracker.IsEmpty() or batch.Receipt.IsValid(), "Either no textures in the batch, or the batch is ended." );
    if ( not batch.Tracker.IsEmpty() and batch.Receipt.IsComplete() )
    {
      auto lock_guard = std::lock_guard( m_LoadLock );

      batch.ClearResources( &m_PendingBarriers );
    }
  }
}

void Ember::TextureLoader::FlushBarriers( ID3D12GraphicsCommandList* command_list )
{
  if ( m_PendingBarriers.empty() ) return;

  auto lock_guard = std::lock_guard( m_LoadLock );

  command_list->ResourceBarrier( CountOf( m_PendingBarriers ), DataOf( m_PendingBarriers ) );

  m_PendingBarriers.clear();
}
