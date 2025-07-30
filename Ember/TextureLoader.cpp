#include "TextureLoader.hpp"

#include "Util/HelperUtils.hpp"

#include <DirectXTex.h>
#include <span>

#include "RenderDevice.hpp"
#include "Util/DataUtil.hpp"

struct MipMapRootSigInfo
{
  DirectX::XMFLOAT2 TexelSize;
  Ember::UAVHandle  Src;
  Ember::UAVHandle  Dst;
};

Ember::TextureLoader::UploadBatch::UploadBatch( std::pmr::polymorphic_allocator<> const& pool_allocator )
  : Intermediate{ pool_allocator }
{}

#if not defined( RENDERDOC_COMPAT )
void Ember::TextureLoader::UploadBatch::PushUpload(
    ComPtr<ID3D12Resource> dest, ComPtr<D3D12MA::Allocation> intermediate )
#else
void Ember::TextureLoader::UploadBatch::PushUpload( ComPtr<ID3D12Resource> dest, ComPtr<ID3D12Resource> intermediate )
#endif
{
  Textures.push_front( std::move( dest ) );
  Intermediate.push_front( std::move( intermediate ) );
}

void Ember::TextureLoader::UploadBatch::PushAlias( ComPtr<ID3D12Resource> alias )
{
  Aliases.emplace_front( std::move( alias ) );
}

void Ember::TextureLoader::UploadBatch::ClearResources()
{
  Textures.clear();
  Intermediate.clear();
  Aliases.clear();
}

DXGI_FORMAT MakeUAVCompat( DXGI_FORMAT format )
{
  switch ( format )
  {
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
      return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_R8G8B8A8_UNORM:
      return format;
    default:
      UNIMPLEMENTED_M( "Add formats as used/required" );
  }
}

// Thread unsafe
bool Ember::TextureLoader::TryGenerateMipMaps( ID3D12GraphicsCommandList* command_list, Texture* texture )
{
  ComPtr<ID3D12Resource> alias;

  // Grab the 'necessary' info from texture, and copy it to the 'alias' resource.
#if not defined( RENDERDOC_COMPAT )
  D3D12MA::Allocation* allocation = texture->GetAllocation();
  ID3D12Resource*      resource   = allocation->GetResource();
#else
  ID3D12Resource* resource = texture->GetTexture();
#endif

  D3D12_RESOURCE_DESC desc  = resource->GetDesc();
  desc.Flags               &= ~( D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL );
  desc.Flags               |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

  desc.Format               = MakeUAVCompat( desc.Format );

#if not defined( RENDERDOC_COMPAT )
  ERR_FAIL_RET_V(
      m_RenderDevice->GetAllocator()->CreateAliasingResource(
          allocation, 0, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS( &alias ) ),
      false );
#else
  CD3DX12_HEAP_PROPERTIES properties{ D3D12_HEAP_TYPE_DEFAULT };
  ERR_FAIL_RET_V(
      m_RenderDevice->GetDevice()->CreateCommittedResource(
          &properties, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS( &alias ) ),
      false );
#endif

  ERR_FAIL_RET_V( alias->SetName( L"UAV Alias" ), false );
  {
    CD3DX12_RESOURCE_BARRIER const transition = CD3DX12_RESOURCE_BARRIER::Transition(
        resource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE );
    command_list->ResourceBarrier( 1, &transition );
  }
  command_list->CopyResource( alias.Get(), resource );

  std::vector<UAVHandle> mip_handles;
  for ( int level = 0; level < desc.MipLevels; level++ )
  {
    CD3DX12_UNORDERED_ACCESS_VIEW_DESC uav_desc = CD3DX12_UNORDERED_ACCESS_VIEW_DESC::Tex2D( desc.Format, level );
    mip_handles.push_back( m_RenderDevice->CreateBindlessHandle( alias.Get(), uav_desc ) );
  }

  uint64_t tex_width  = desc.Width;
  uint64_t tex_height = desc.Height;

  m_UploadBatches[m_CurrentUploadBatch].PushAlias( alias );

  CD3DX12_RESOURCE_BARRIER pre_compute_barrier[] = {
#if not defined( RENDERDOC_COMPAT )
    CD3DX12_RESOURCE_BARRIER::Aliasing( allocation->GetResource(), alias.Get() ),
#endif
    CD3DX12_RESOURCE_BARRIER::Transition(
        alias.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS )
  };

  command_list->ResourceBarrier( CountOf( pre_compute_barrier ), DataOf( pre_compute_barrier ) );

  auto bindless_desc_heaps = m_RenderDevice->GetBindlessDescriptorHeaps();

  command_list->SetPipelineState( m_MipmapPipeline.Get() );
  command_list->SetComputeRootSignature( m_MipMapRootSig.Get() );
  command_list->SetDescriptorHeaps( CountOf( bindless_desc_heaps ), DataOf( bindless_desc_heaps ) );

  MipMapRootSigInfo        mip_map_info;

  CD3DX12_RESOURCE_BARRIER inter_mip_barrier = CD3DX12_RESOURCE_BARRIER::UAV( alias.Get() );
  for ( int write_lvl = 1; write_lvl < desc.MipLevels; write_lvl++ )
  {
    tex_width              = std::max( tex_width / 2, 1llu );
    tex_height             = std::max( tex_height / 2, 1llu );

    mip_map_info.TexelSize = { 1.0f / ( float )tex_width, 1.0f / ( float )tex_height };
    mip_map_info.Src       = mip_handles[write_lvl - 1];
    mip_map_info.Dst       = mip_handles[write_lvl];

    command_list->SetComputeRoot32BitConstants( 0, sizeof( MipMapRootSigInfo ) / 4, &mip_map_info, 0 );
    command_list->Dispatch(
        std::max<UINT>( 1, ( UINT )( tex_width / 8 ) ), std::max<UINT>( 1, ( UINT )( tex_height / 8 ) ), 1 );
    command_list->ResourceBarrier( 1, &inter_mip_barrier );
  }

  {
    CD3DX12_RESOURCE_BARRIER barriers[] = {
      CD3DX12_RESOURCE_BARRIER::Transition(
          alias.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE ),
      CD3DX12_RESOURCE_BARRIER::Transition(
          resource, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST ),
    };
    command_list->ResourceBarrier( CountOf( barriers ), DataOf( barriers ) );
  }
  command_list->CopyResource( resource, alias.Get() );

#if not defined( RENDERDOC_COMPAT )
  {
    CD3DX12_RESOURCE_BARRIER reverse_aliasing = CD3DX12_RESOURCE_BARRIER::Aliasing( alias.Get(), resource );
    command_list->ResourceBarrier( 1, &reverse_aliasing );
  }
#endif

  return true;
}

Ember::TextureLoader::TextureLoader(
    RenderDevice*               render_device,
    ComPtr<ID3D12RootSignature> mipmap_root_signature,
    ComPtr<ID3D12PipelineState> mipmap_pipeline,
    Context                     copy_context,
    uint32_t const              upload_frame_count )
  : m_RenderDevice{ render_device }
  , m_CopyContext{ std::move( copy_context ) }
  , m_MipMapRootSig{ std::move( mipmap_root_signature ) }
  , m_MipmapPipeline{ std::move( mipmap_pipeline ) }
{
  m_UploadBatches.reserve( upload_frame_count );
  for ( int i = 0; i < ( int )upload_frame_count; ++i )
  {
    m_UploadBatches.emplace_back( &m_InFlightPool );
  }

  m_CurrentCommandList = m_CopyContext.GetCommandList();
}

void Ember::TextureLoader::Create( TextureLoader* loader, RenderDevice* render_device, uint32_t upload_frame_count )
{
  ComPtr<ID3D12Device2> device = render_device->GetDevice();

  Context               transfer_context;
  // TODO: This shouldn't be a compute context.
  // We need this due to the mip-mapping.
  // Ideally, we want to kick the job to an async compute queue.
  // Effectively TYPE_COPY (transfer) -> TYPE_COMPUTE (mipmap) -> TYPE_DIRECT (actual use)
  Context::Create( &transfer_context, device, D3D12_COMMAND_LIST_TYPE_COMPUTE );

  ComPtr<ID3DBlob> mipmap_shader_blob;
  ERR_ABORT( D3DReadFileToBlob( L"MipMap.cso", &mipmap_shader_blob ) );

  D3D12_FEATURE_DATA_ROOT_SIGNATURE feature_data;
  feature_data.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
  if ( FAILED( device->CheckFeatureSupport( D3D12_FEATURE_ROOT_SIGNATURE, &feature_data, sizeof( feature_data ) ) ) )
  {
    feature_data.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
  }

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
  root_parameters[0].InitAsConstants( sizeof( MipMapRootSigInfo ) / 4, 0, 0, D3D12_SHADER_VISIBILITY_ALL );

  CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC root_signature_desc;
  root_signature_desc.Init_1_1(
      CountOf( root_parameters ), DataOf( root_parameters ), 1, &static_sampler_desc, root_signature_flags );

  ComPtr<ID3DBlob> root_signature_blob;
  ComPtr<ID3DBlob> error_blob;
  ERR_ABORT( D3DX12SerializeVersionedRootSignature(
      &root_signature_desc, feature_data.HighestVersion, &root_signature_blob, &error_blob ) );

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
    .CS            = CD3DX12_SHADER_BYTECODE( mipmap_shader_blob.Get() ),
  };

  D3D12_PIPELINE_STATE_STREAM_DESC const pipeline_state_stream_desc = {
    .SizeInBytes                   = sizeof pipeline_stream,
    .pPipelineStateSubobjectStream = &pipeline_stream,
  };

  ComPtr<ID3D12PipelineState> pipeline_state;
  ERR_ABORT( device->CreatePipelineState( &pipeline_state_stream_desc, IID_PPV_ARGS( &pipeline_state ) ) );

  new ( loader ) TextureLoader{
    render_device,      std::move( root_signature ), std::move( pipeline_state ), std::move( transfer_context ),
    upload_frame_count,
  };
}

bool Ember::TextureLoader::TryLoadImpl(
    Texture* texture, char const* id, DirectX::TexMetadata const& metadata, DirectX::ScratchImage const& scratch_image )
{
  std::span const images{ scratch_image.GetImages(), scratch_image.GetImageCount() };

  new ( texture ) Texture{ m_RenderDevice->CreateTexture2D(
      metadata.format, ( uint32_t )metadata.width, ( uint32_t )metadata.height ) };

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
  m_UploadBatches[m_CurrentUploadBatch].PushUpload( texture->GetTexture(), staging_alloc );
#else
  m_UploadBatches[m_CurrentUploadBatch].PushUpload( texture->GetTexture(), staging_res );
#endif

  if ( not TryGenerateMipMaps( m_CurrentCommandList.Get(), texture ) )
  {
    return true;
  }

  m_Cache[id] = *texture;

  return false;
}

bool Ember::TextureLoader::TryLoadTexture( Texture* texture, char const* filename )
{
  auto const it = m_Cache.find( filename );
  if ( it != m_Cache.end() )
  {
    new ( texture ) Texture{ it->second };
  }

  std::filesystem::path const file_path( filename );
  if ( not exists( file_path ) ) return false;

  wchar_t wide_filename[512];
  MultiByteToWideChar( CP_UTF8, MB_ERR_INVALID_CHARS, filename, -1, wide_filename, 512 );

  DirectX::TexMetadata  metadata;
  DirectX::ScratchImage scratch_image;
  ERR_FAIL_RET_V( LoadFromWICFile( wide_filename, DirectX::WIC_FLAGS_DEFAULT_SRGB, &metadata, scratch_image ), false );

  return TryLoadImpl( texture, filename, metadata, scratch_image );
}

bool Ember::TextureLoader::TryLoadTextureFromData(
    Texture*           texture,
    char const*        id,
    size_t const       data_size,
    byte const*        data,
    ColorSpaceOverride color_space_override )
{
  auto const it = m_Cache.find( id );
  if ( it != m_Cache.end() )
  {
    new ( texture ) Texture{ it->second };
  }

  DirectX::TexMetadata  metadata;
  DirectX::ScratchImage scratch_image;
  DirectX::WIC_FLAGS    flags = DirectX::WIC_FLAGS_NONE;
  switch ( color_space_override )
  {
    case ColorSpaceOverride::kNone:
      flags |= DirectX::WIC_FLAGS_DEFAULT_SRGB;
      break;
    case ColorSpaceOverride::kLinear:
      flags |= DirectX::WIC_FLAGS_FORCE_LINEAR;
      break;
    case ColorSpaceOverride::kSrgb:
      flags |= DirectX::WIC_FLAGS_FORCE_SRGB;
      break;
  }
  ERR_FAIL_RET_V( DirectX::LoadFromWICMemory( data, data_size, flags, &metadata, scratch_image ), false );

  return TryLoadImpl( texture, id, metadata, scratch_image );
}

Ember::Context::Receipt Ember::TextureLoader::EndBatch()
{
  // TODO:
  // This is no good.
  // The 3 frame delay isn't a good system.
  ERR_ABORT( m_CurrentCommandList->Close() );

  m_UploadBatches[m_CurrentUploadBatch].Receipt = m_CopyContext.Submit( std::move( m_CurrentCommandList ) );

  Context::Receipt const batch_receipt          = m_UploadBatches[m_CurrentUploadBatch].Receipt;

  m_CurrentUploadBatch++;
  m_CurrentUploadBatch %= m_UploadBatches.size();

  m_CopyContext.WaitOn( m_UploadBatches[m_CurrentUploadBatch].Receipt );

  auto lock_guard = std::lock_guard( m_LoadLock );
  for ( auto texture : m_UploadBatches[m_CurrentUploadBatch].Textures )
  {
    m_PendingBarriers.push_back( CD3DX12_RESOURCE_BARRIER::Transition(
        texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE ) );
  }

  m_UploadBatches[m_CurrentUploadBatch].ClearResources();

  m_CurrentCommandList     = m_CopyContext.GetCommandList();
  m_CurrentUploadBatchSize = 0;

  return batch_receipt;
}

void Ember::TextureLoader::Update()
{
  // TODO: Ideally, I don't want this as a check on 'Update' but instead it should be a task on a thread pool.
  for ( auto& batch : m_UploadBatches )
  {
    if ( not batch.Textures.empty() and batch.Receipt.IsComplete() )
    {
      auto lock_guard = std::lock_guard( m_LoadLock );
      for ( auto texture : batch.Textures )
      {
        m_PendingBarriers.push_back( CD3DX12_RESOURCE_BARRIER::Transition(
            texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE ) );
      }

      batch.ClearResources();
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
