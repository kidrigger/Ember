#pragma once

#include <DirectXTex.h>
#include <filesystem>
#include <memory_resource>
#include <span>
#include <string>
#include <unordered_map>

#include "BindlessManager.hpp"
#include "Context.hpp"
#include "Texture.hpp"

namespace Ember
{
class RenderDevice;

enum class ColorSpaceOverride
{
  kNone,
  kLinear,
  kSrgb,
};

class TextureLoader
{
#if not defined( RENDERDOC_COMPAT )
  using UploadIntermediate = ComPtr<D3D12MA::Allocation>;
#else
  using UploadIntermediate = ComPtr<ID3D12Resource>;
#endif
  using UploadIntermediateList = std::pmr::forward_list<UploadIntermediate>;
  using UploadTextureList      = std::pmr::forward_list<ComPtr<ID3D12Resource>>;
  using UploadAliasList        = std::pmr::forward_list<ComPtr<ID3D12Resource>>;
  using UploadHandleList       = std::vector<std::variant<SRVHandle, UAVHandle>>;

  struct UploadBatch
  {
    RenderDevice*          Device;
    UploadIntermediateList Intermediate;
    UploadTextureList      Textures;
    UploadAliasList        Aliases;
    UploadHandleList       Handles;
    Context::Receipt       Receipt;

    UploadBatch() = default;
    explicit UploadBatch( RenderDevice* render_device, std::pmr::polymorphic_allocator<> const& pool_allocator );
#if not defined( RENDERDOC_COMPAT )
    void PushUpload( ComPtr<ID3D12Resource> dest, ComPtr<D3D12MA::Allocation> intermediate );
#else
    void PushUpload( ComPtr<ID3D12Resource> dest, ComPtr<ID3D12Resource> intermediate );
#endif
    void PushAlias( ComPtr<ID3D12Resource> alias );
    void PushHandle( SRVHandle handle );
    void PushHandles( std::span<UAVHandle> handles );
    void ClearResources();
  };

  using TextureCache = std::pmr::unordered_map<std::pmr::string, Texture>;

  RenderDevice*                          m_RenderDevice;
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

  // Mips
  ComPtr<ID3D12RootSignature> m_MipMapRootSig;
  ComPtr<ID3D12PipelineState> m_MipmapPipeline;

  //
  bool TryGenerateMipMaps( ID3D12GraphicsCommandList* command_list, Texture* texture );
  bool TryLoadImpl(
      Ember::Texture*              texture,
      char const*                  id,
      DirectX::TexMetadata const&  metadata,
      DirectX::ScratchImage const& scratch_image,
      ColorSpaceOverride           color_space_override );

public:
  TextureLoader() = default;

  TextureLoader(
      RenderDevice*               render_device,
      ComPtr<ID3D12RootSignature> mipmap_root_signature,
      ComPtr<ID3D12PipelineState> mipmap_pipeline,
      Context                     copy_context,
      uint32_t                    upload_frame_count );

  static void Create( TextureLoader* loader, RenderDevice* render_device, uint32_t upload_frame_count );

  bool        TryLoadTexture(
             Texture* texture, char const* filename, ColorSpaceOverride color_space_override = ColorSpaceOverride::kNone );
  bool TryLoadTextureFromData(
      Texture*           texture,
      char const*        id,
      size_t             data_size,
      byte const*        data,
      ColorSpaceOverride color_space_override = ColorSpaceOverride::kNone );
  Context::Receipt EndBatch();

  void             Update();
  void             FlushBarriers( ID3D12GraphicsCommandList* command_list );
};

} // namespace Ember
