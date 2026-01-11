#pragma once

#include <Graphics/Buffer.hpp>
#include <Graphics/RenderDevice.hpp>
#include <Util/HelperUtils.hpp>
#include <Util/Runtime.hpp>

namespace Ember
{

TYPED_HANDLE( Geometry, GeometryManager );

class GeometryAllocation
{
  Buffer                        m_Buffer;
  ComPtr<D3D12MA::VirtualBlock> m_Block;
  D3D12MA::VirtualAllocation    m_Allocation;
  size_t                        m_Offset;

public:
  GeometryAllocation() = default;
  GeometryAllocation(
      Buffer buffer, ComPtr<D3D12MA::VirtualBlock> block, D3D12MA::VirtualAllocation allocation, size_t offset );

  void                                    Write( size_t offset, size_t size, void const* data ) const;
  [[nodiscard]] size_t                    GetOffsetInBytes() const;
  [[nodiscard]] D3D12_GPU_VIRTUAL_ADDRESS GetGPUVirtualAddress() const;     //< Local GPU virtual address.
  [[nodiscard]] D3D12_GPU_VIRTUAL_ADDRESS GetBaseGPUVirtualAddress() const; //< Global GPU virtual address.

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

  [[nodiscard]] static bool Create(
      GeometryManager* geometry_manager, RenderDevice* render_device, size_t total_ugb_size );

  [[nodiscard]] GeometryAllocation CreateGeometry( size_t geometry_size, size_t geometry_alignment );

  [[nodiscard]] SRVHandle          GetSRVHandle() const;
};

} // namespace Ember
