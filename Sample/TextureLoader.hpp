#pragma once

#include <memory_resource>
#include <span>
#include <string>
#include <unordered_map>

#include <Graphics/CommandList.hpp>
#include <Graphics/Queue.hpp>
#include <Graphics/Texture.hpp>

#include "MipMapGenerator.hpp"

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
    BarrierList    Barriers;
    Queue::Receipt Receipt;

    UploadBatch() = default;
    explicit UploadBatch( Queue::Receipt receipt, std::pmr::polymorphic_allocator<> const& pool_allocator );
    void PushTextureStateChange( Texture dest, D3D12_RESOURCE_STATES const final_state );
    void FlushPendingBarriers( std::vector<D3D12_RESOURCE_BARRIER>* barriers );
  };

  using TextureCache = std::pmr::unordered_map<std::pmr::string, Texture>;

  RenderDevice*                          m_RenderDevice;
  MipMapGenerator*                       m_MipMapGenerator;

  std::pmr::unsynchronized_pool_resource m_CachePool;
  TextureCache                           m_Cache;
  std::mutex                             m_LoadLock;

  // Upload
  std::pmr::unsynchronized_pool_resource m_InFlightPool;
  std::shared_ptr<Queue>                 m_CopyContext;
  std::vector<UploadBatch>               m_UploadBatches;
  uint32_t                               m_CurrentUploadBatch{ 0 };
  CommandList                            m_CurrentCommandList;
  uint32_t                               m_CurrentUploadBatchSize{ 0 };

  std::vector<D3D12_RESOURCE_BARRIER>    m_PendingBarriers;


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
      RenderDevice*          render_device,
      MipMapGenerator*       mipmap_generator,
      std::shared_ptr<Queue> copy_context,
      uint32_t               upload_frame_count );

  static bool Create(
      TextureLoader*         loader,
      RenderDevice*          render_device,
      MipMapGenerator*       mipmap_generator,
      std::shared_ptr<Queue> compute_context,
      uint32_t               upload_frame_count );

  [[nodiscard]] MipMapGenerator* GetMipMapper() const;

  // Loaders
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

  Queue::Receipt EndBatch();

  void           Update();
  void           FlushBarriers( CommandList* command_list );
};

} // namespace Ember
