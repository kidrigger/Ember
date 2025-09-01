#pragma once

#include "Buffer.hpp"
#include "Material.hpp"

namespace Ember
{
class RenderDevice;

class MaterialManager
{
  // Update ElementBits if changed
  // Page is 4KB multiple.
  // (12 KB / 48 bytes) = 256;
  uint8_t constexpr static kElementBits       = 8;
  uint32_t constexpr static kElementsPerPage  = 1 << kElementBits;
  uint32_t constexpr static kElementIndexMask = kElementsPerPage - 1;
  uint32_t constexpr static kPageIndexMask    = ~kElementIndexMask;
  uint32_t constexpr static kPageIndexOffset  = kElementBits;

  using Page                                  = std::array<MaterialImpl::GpuRepr, kElementsPerPage>;

  RabbitPullingFreeList m_FreeList;
  std::deque<Page>      m_Pages;
  std::deque<bool>      m_PageDirty;
  Buffer                m_DataBuffer;
  std::mutex            m_Lock;

public:
  MaterialManager() = default;
  MaterialManager( Buffer data_buffer, uint32_t max_materials );

  static void Create( MaterialManager* material_manager, RenderDevice* render_device, uint32_t max_materials );

  [[nodiscard]] MaterialHandle CreateMaterialHandle( MaterialImpl::GpuRepr const& material );
  void                         Free( MaterialHandle handle );

  void                         UpdateReprs();
  [[nodiscard]] SRVHandle      PrepareFrame();

  MaterialManager( MaterialManager const& other )                = delete;
  MaterialManager( MaterialManager&& other ) noexcept            = delete;
  MaterialManager& operator=( MaterialManager const& other )     = delete;
  MaterialManager& operator=( MaterialManager&& other ) noexcept = delete;
  ~MaterialManager();
};

} // namespace Ember
