#pragma once

#include <memory_resource>
#include <span>
#include <string>
#include <unordered_map>

#include <Graphics/CommandList.hpp>
#include <Graphics/Context.hpp>
#include <Graphics/Texture.hpp>
#include "ResourceTracker.hpp"

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
  using UploadIntermediateList = std::pmr::forward_list<ComPtr<IUnknown>>;
  using UploadTextureList      = std::pmr::forward_list<ComPtr<ID3D12Resource>>;
  using UploadAliasList        = std::pmr::forward_list<ComPtr<ID3D12Resource>>;
  using UploadHandleList       = std::vector<std::variant<SRVHandle, UAVHandle>>;

  struct UploadBatch
  {
    ResourceTracker  Tracker;
    Context::Receipt Receipt;

    UploadBatch() = default;
    explicit UploadBatch(
        RenderDevice*                            render_device,
        Context::Receipt                         receipt,
        std::pmr::polymorphic_allocator<> const& pool_allocator );
    void PushUpload(
        ComPtr<ID3D12Resource> dest, ComPtr<IUnknown> intermediate, D3D12_RESOURCE_STATES const final_state );
    void PushAllocation( ComPtr<D3D12MA::Allocation> intermediate );
    void PushAlias( ComPtr<ID3D12Resource> alias );
    void PushHandle( SRVHandle handle );
    void PushHandles( std::span<UAVHandle> const& handles );
    void ClearResources( std::vector<D3D12_RESOURCE_BARRIER>* barriers );
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
  CommandList                            m_CurrentCommandList;
  uint32_t                               m_CurrentUploadBatchSize{ 0 };

  std::vector<D3D12_RESOURCE_BARRIER>    m_PendingBarriers;

  // Mips
  ComPtr<ID3D12RootSignature> m_MipMapRootSig;
  ComPtr<ID3D12PipelineState> m_MipmapPipeline;
  ComPtr<ID3D12PipelineState> m_MipmapCubePipeline;

  //
  bool TryLoadImpl(
      Texture*                     texture,
      char const*                  id,
      DirectX::TexMetadata const&  metadata,
      DirectX::ScratchImage const& scratch_image,
      ColorSpaceOverride const     color_space_override,
      D3D12_RESOURCE_STATES        final_state );

public:
  TextureLoader() = default;

  TextureLoader(
      RenderDevice*               render_device,
      ComPtr<ID3D12RootSignature> mipmap_root_signature,
      ComPtr<ID3D12PipelineState> mipmap_pipeline,
      ComPtr<ID3D12PipelineState> mipmap_cube_pipeline,
      Context                     copy_context,
      uint32_t                    upload_frame_count );

  static void Create( TextureLoader* loader, RenderDevice* render_device, uint32_t upload_frame_count );

  bool        TryLoadTexture(
             Texture*           texture,
             char const*        filename,
             ColorSpaceOverride color_space_override = ColorSpaceOverride::kNone,
             D3D12_RESOURCE_STATES final_state       = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE );

  bool TryLoadTextureFromData(
      Texture*           texture,
      char const*        id,
      size_t             data_size,
      byte const*        data,
      ColorSpaceOverride color_space_override = ColorSpaceOverride::kNone,
      D3D12_RESOURCE_STATES final_state       = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE );

  bool TryGenerateMipMaps( ID3D12GraphicsCommandList* command_list, Texture* texture, ResourceTracker* tracker ) const;
  bool TryGenerateMipMapCube(
      ID3D12GraphicsCommandList* command_list,
      Texture*                   texture,
      ResourceTracker*           tracker,
      D3D12_RESOURCE_STATES      texture_resource_state ) const;
  Context::Receipt EndBatch();

  void             Update();
  void             FlushBarriers( ID3D12GraphicsCommandList* command_list );
};

} // namespace Ember
