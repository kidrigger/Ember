#pragma once

#include "Buffer.hpp"
#include "RenderDevice.hpp"
#include "Util/HelperUtils.hpp"
#include "Util/Runtime.hpp"

namespace Ember
{

class GeometryAllocation
{
  Buffer                        m_Buffer;
  ComPtr<D3D12MA::VirtualBlock> m_Block;
  D3D12MA::VirtualAllocation    m_Allocation;
  uint32_t                      m_Offset;

public:
  GeometryAllocation() = default;
  GeometryAllocation(
      Buffer buffer, ComPtr<D3D12MA::VirtualBlock> block, D3D12MA::VirtualAllocation allocation, uint32_t offset );

  void     Write( uint32_t offset, uint32_t size, void const* data ) const;
  uint32_t GetOffsetInBytes() const;

  GeometryAllocation( GeometryAllocation const& other ) = delete;
  GeometryAllocation( GeometryAllocation&& other ) noexcept;
  GeometryAllocation& operator=( GeometryAllocation const& other ) = delete;
  GeometryAllocation& operator=( GeometryAllocation&& other ) noexcept;

  ~GeometryAllocation();
};

class GeometryManager
{
  Buffer                        m_UnifiedGeometryBuffer;
  ComPtr<D3D12MA::VirtualBlock> m_GeometryBufferAllocator;

public:
  GeometryManager() = default;
  GeometryManager( Buffer unified_geometry_buffer, ComPtr<D3D12MA::VirtualBlock> geometry_buffer_allocator );

  [[nodiscard]] static bool Create( GeometryManager* geometry_manager, RenderDevice* render_device, uint32_t const total_ugb_size );

  GeometryAllocation      CreateGeometry( uint32_t const geometry_size, uint32_t const geometry_alignment );

  [[nodiscard]] SRVHandle GetSRVHandle() const;
};

} // namespace Ember
