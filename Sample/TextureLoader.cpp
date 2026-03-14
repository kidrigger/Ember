#include "TextureLoader.hpp"

#include <filesystem>
#include <span>

#include <Graphics/RenderDevice.hpp>
#include <Util/DataUtil.hpp>
#include <Util/DirectXHeaders.hpp>
#include <Util/HelperUtils.hpp>
#include <Util/Profiling.hpp>
#include <Util/StringUtil.hpp>
#include "MipMapGenerator.hpp"

Ember::TextureLoader::UploadBatch::UploadBatch(
    Queue::Receipt receipt, std::pmr::polymorphic_allocator<> const& pool_allocator )
  : Barriers{ pool_allocator }, Receipt{ std::move( receipt ) }
{}

void Ember::TextureLoader::UploadBatch::PushTextureStateChange( Texture dest, D3D12_RESOURCE_STATES const final_state )
{
  Barriers.emplace_back( dest, final_state );
}

void Ember::TextureLoader::UploadBatch::FlushPendingBarriers( std::vector<D3D12_RESOURCE_BARRIER>* barriers )
{
  std::ranges::transform(
      Barriers,
      std::back_inserter( *barriers ),
      []( auto const& pair )
      {
        D3D12_RESOURCE_BARRIER const transition =
            CD3DX12_RESOURCE_BARRIER::Transition( pair.first.GetTexture(), pair.first.GetCurrentState(), pair.second );
        pair.first.SetCurrentState( pair.second );
        return transition;
      } );

  Barriers.clear();
}

Ember::TextureLoader::TextureLoader(
    RenderDevice*          render_device,
    MipMapGenerator*       mipmap_generator,
    std::shared_ptr<Queue> copy_context,
    uint32_t const         upload_frame_count )
  : m_RenderDevice{ render_device }, m_CopyContext{ std::move( copy_context ) }, m_MipMapGenerator{ mipmap_generator }
{
  m_UploadBatches.reserve( upload_frame_count );
  Queue::Receipt initial = m_CopyContext->CreateReceipt();
  for ( int i = 0; i < ( int )upload_frame_count; ++i )
  {
    m_UploadBatches.emplace_back( initial, &m_InFlightPool );
  }
  m_CurrentCommandList = m_CopyContext->GetCommandList();
}

bool Ember::TextureLoader::Create(
    TextureLoader*         loader,
    RenderDevice*          render_device,
    MipMapGenerator*       mipmap_generator,
    std::shared_ptr<Queue> compute_context,
    uint32_t const         upload_frame_count )
{
  if ( compute_context->GetCommandListType() != D3D12_COMMAND_LIST_TYPE_COMPUTE )
  {
    OutputDebugStringA( "TextureLoader requires a compute context for mip-map generation. " );
    return false;
  }

  // We need COMPUTE instead of COPY due to the mip-mapping.
  // Ideally, we want to kick the job to an async compute queue.

  new ( loader ) TextureLoader{
    render_device,
    mipmap_generator,
    std::move( compute_context ),
    upload_frame_count,
  };

  return true;
}

Ember::MipMapGenerator* Ember::TextureLoader::GetMipMapper() const
{
  return m_MipMapGenerator;
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

  new ( texture ) Texture{ m_RenderDevice->CreateTexture2D( {
      .Format    = format,
      .Width     = ( uint32_t )metadata.width,
      .Height    = ( uint32_t )metadata.height,
      .InitState = D3D12_RESOURCE_STATE_COPY_DEST,
  } ) };

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

    m_CurrentCommandList.Track( std::move( staging_alloc ) );
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

  m_CurrentCommandList.Track( staging_res );

  wchar_t staging_name[512];
  FormatTo( staging_name, L"Staging: {}", wide_id );
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

  m_UploadBatches[m_CurrentUploadBatch].PushTextureStateChange( *texture, final_state );

  if ( not m_MipMapGenerator->TryGenerateMipMaps( &m_CurrentCommandList, texture ) )
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

Ember::Queue::Receipt Ember::TextureLoader::EndBatch()
{
  m_UploadBatches[m_CurrentUploadBatch].Receipt = m_CopyContext->Submit( std::move( m_CurrentCommandList ) );

  Queue::Receipt const batch_receipt            = m_UploadBatches[m_CurrentUploadBatch].Receipt;

  m_CurrentUploadBatch++;
  m_CurrentUploadBatch %= m_UploadBatches.size();

  m_CopyContext->WaitOn( m_UploadBatches[m_CurrentUploadBatch].Receipt );

  auto lock_guard = std::lock_guard( m_LoadLock );

  m_UploadBatches[m_CurrentUploadBatch].FlushPendingBarriers( &m_PendingBarriers );

  m_CurrentCommandList     = m_CopyContext->GetCommandList();
  m_CurrentUploadBatchSize = 0;

  return batch_receipt;
}

void Ember::TextureLoader::Update()
{
  // TODO: Ideally, I don't want this as a check on 'Update' but instead it should be a task on a thread pool.
  for ( auto& batch : m_UploadBatches )
  {
    ASSERT_M(
        batch.Barriers.empty() or batch.Receipt.IsValid(), "Either no textures in the batch, or the batch is ended." );
    if ( not batch.Barriers.empty() and batch.Receipt.IsComplete() )
    {
      auto lock_guard = std::lock_guard( m_LoadLock );

      batch.FlushPendingBarriers( &m_PendingBarriers );
    }
  }
}

void Ember::TextureLoader::FlushBarriers( CommandList* command_list )
{
  if ( m_PendingBarriers.empty() ) return;

  auto lock_guard = std::lock_guard( m_LoadLock );

  command_list->ResourceBarrier( m_PendingBarriers );

  m_PendingBarriers.clear();
}
