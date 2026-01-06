#pragma once

#include <memory_resource>
#include <span>
#include <string>
#include <unordered_map>

#include <Graphics/CommandList.hpp>
#include <Graphics/Context.hpp>
#include <Graphics/Texture.hpp>

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
  struct UploadBatch
  {
    using BarrierList = std::pmr::deque<std::pair<Texture, D3D12_RESOURCE_STATES>>;
    BarrierList      Barriers;
    Context::Receipt Receipt;

    UploadBatch() = default;
    explicit UploadBatch( Context::Receipt receipt, std::pmr::polymorphic_allocator<> const& pool_allocator );
    void PushTextureStateChange( Texture dest, D3D12_RESOURCE_STATES const final_state );
    void FlushPendingBarriers( std::vector<D3D12_RESOURCE_BARRIER>* barriers );
  };

  using TextureCache = std::pmr::unordered_map<std::pmr::string, Texture>;

  RenderDevice*                          m_RenderDevice;
  std::pmr::unsynchronized_pool_resource m_CachePool;
  TextureCache                           m_Cache;
  std::mutex                             m_LoadLock;

  // Upload
  std::pmr::unsynchronized_pool_resource m_InFlightPool;
  std::shared_ptr<Context>               m_CopyContext;
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
      std::shared_ptr<Context>    copy_context,
      uint32_t                    upload_frame_count );

  static bool Create(
      TextureLoader*           loader,
      RenderDevice*            render_device,
      std::shared_ptr<Context> compute_context,
      uint32_t                 upload_frame_count );

  bool TryLoadTexture(
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

  bool             TryGenerateMipMaps( CommandList* command_list, Texture* texture ) const;
  bool             TryGenerateMipMapCube( CommandList* command_list, Texture* texture ) const;
  Context::Receipt EndBatch();

  void             Update();
  void             FlushBarriers( CommandList* command_list );
};

} // namespace Ember
