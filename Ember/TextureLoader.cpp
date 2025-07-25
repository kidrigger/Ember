#include "TextureLoader.hpp"

#include "Util/HelperUtils.hpp"

#include <DirectXTex.h>
#include <span>

#include "RenderDevice.hpp"
#include "Util/DataUtil.hpp"

Ember::TextureLoader::UploadBatch::UploadBatch( std::pmr::polymorphic_allocator<> const& pool_allocator )
  : Intermediate{ pool_allocator }, Receipt{}
{}

void Ember::TextureLoader::UploadBatch::PushUpload( ComPtr<ID3D12Resource> dest, UploadIntermediate intermediate )
{
  Textures.push_front( std::move( dest ) );
  Intermediate.push_front( std::move( intermediate ) );
}

void Ember::TextureLoader::UploadBatch::ClearResources()
{
  Textures.clear();
  Intermediate.clear();
}

Ember::TextureLoader::TextureLoader(
    ComPtr<ID3D12Device2>      device,
    ComPtr<D3D12MA::Allocator> allocator,
    BindlessManager*           bindless_manager,
    TextureManager*            texture_manager,
    Context                    copy_context,
    uint32_t const             upload_frame_count )
  : m_Device{ std::move( device ) }
  , m_Allocator{ std::move( allocator ) }
  , m_BindlessManager{ bindless_manager }
  , m_TextureManager{ texture_manager }
  , m_CopyContext{ std::move( copy_context ) }
{
  m_UploadBatches.reserve( upload_frame_count );
  for ( int i = 0; i < ( int )upload_frame_count; ++i )
  {
    m_UploadBatches.emplace_back( &m_InFlightPool );
  }

  m_CurrentCommandList = m_CopyContext.GetCommandList();
}

void Ember::TextureLoader::Create(
    TextureLoader*             loader,
    ComPtr<ID3D12Device2>      device,
    ComPtr<D3D12MA::Allocator> allocator,
    BindlessManager*           bindless_manager,
    TextureManager*            texture_manager,
    uint32_t const             upload_frame_count )
{
  Context transfer_context;
  Context::Create( &transfer_context, device, D3D12_COMMAND_LIST_TYPE_COPY );

  new ( loader ) TextureLoader{
    std::move( device ), std::move( allocator ),        bindless_manager,
    texture_manager,     std::move( transfer_context ), upload_frame_count,
  };
}

bool Ember::TextureLoader::TryLoadTexture( Texture* texture, wchar_t const* filename )
{
  auto it = m_Cache.find( filename );
  if ( it != m_Cache.end() )
  {
    new ( texture ) Texture{ it->second };
  }

  std::filesystem::path file_path( filename );
  if ( not exists( file_path ) ) return false;

  DirectX::TexMetadata  metadata;
  DirectX::ScratchImage scratch_image;
  ERR_FAIL_RET_V( LoadFromWICFile( filename, DirectX::WIC_FLAGS_DEFAULT_SRGB, &metadata, scratch_image ), false );

  std::span images{ scratch_image.GetImages(), scratch_image.GetImageCount() };

  new ( texture ) Texture{ m_TextureManager->CreateTexture2D(
      metadata.format, ( uint32_t )metadata.width, ( uint32_t )metadata.height ) };

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
        m_Allocator->CreateResource(
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
        m_Device->CreateCommittedResource(
            &heap_property,
            D3D12_HEAP_FLAG_NONE,
            &resource_desc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS( &staging_res ) ),
        false );
#endif
  }

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

  m_Cache[filename] = *texture;
  // [1] Until here.

  return true;
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
