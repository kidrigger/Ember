#pragma once

#include <filesystem>
#include <memory_resource>
#include <string>
#include <unordered_map>

#include "BindlessManager.hpp"
#include "Context.hpp"
#include "Texture.hpp"

namespace Ember
{

class TextureLoader
{
#if not defined( RENDERDOC_COMPAT )
  using UploadIntermediate = ComPtr<D3D12MA::Allocation>;
#else
  using UploadIntermediate = ComPtr<ID3D12Resource>;
#endif
  using UploadIntermediateList = std::pmr::forward_list<UploadIntermediate>;
  using UploadTextureList      = std::pmr::forward_list<ComPtr<ID3D12Resource>>;

  struct UploadBatch
  {
    UploadIntermediateList Intermediate;
    UploadTextureList      Textures;
    Context::Receipt       Receipt;

    UploadBatch() = default;
    explicit UploadBatch( std::pmr::polymorphic_allocator<> const& pool_allocator );
    void PushUpload( ComPtr<ID3D12Resource> dest, UploadIntermediate intermediate );
    void ClearResources();
  };

  using TextureCache = std::pmr::unordered_map<std::pmr::wstring, Texture>;

  ComPtr<ID3D12Device2>                  m_Device;
  ComPtr<D3D12MA::Allocator>             m_Allocator;
  BindlessManager*                       m_BindlessManager;
  TextureManager*                        m_TextureManager;
  std::pmr::unsynchronized_pool_resource m_CachePool;
  TextureCache                           m_Cache;
  std::mutex                             m_LoadLock;

  // Upload
  std::pmr::unsynchronized_pool_resource m_InFlightPool;
  Context                                m_CopyContext;
  std::vector<UploadBatch>               m_UploadBatches;
  uint32_t                               m_CurrentUploadBatch{ 0 };
  Context::CommandList                   m_CurrentCommandList;
  uint32_t                               m_CurrentUploadBatchSize{ 0 };

  std::vector<D3D12_RESOURCE_BARRIER>    m_PendingBarriers;

public:
  TextureLoader() = default;

  TextureLoader(
      ComPtr<ID3D12Device2>      device,
      ComPtr<D3D12MA::Allocator> allocator,
      BindlessManager*           bindless_manager,
      TextureManager*            texture_manager,
      Context                    copy_context,
      uint32_t                   upload_frame_count );

  static void Create(
      TextureLoader*             loader,
      ComPtr<ID3D12Device2>      device,
      ComPtr<D3D12MA::Allocator> allocator,
      BindlessManager*           bindless_manager,
      TextureManager*            texture_manager,
      uint32_t                   upload_frame_count );

  bool             TryLoadTexture( Texture* texture, wchar_t const* filename );
  Context::Receipt EndBatch();

  void             Update();
  void             FlushBarriers( ID3D12GraphicsCommandList* command_list );
};

} // namespace Ember
