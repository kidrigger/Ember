#include "GeometryManager.hpp"

Ember::GeometryAllocation::GeometryAllocation(
    Buffer buffer, ComPtr<D3D12MA::VirtualBlock> block, D3D12MA::VirtualAllocation allocation, uint32_t const offset )
  : m_Buffer{ std::move( buffer ) }
  , m_Block{ std::move( block ) }
  , m_Allocation{ std::move( allocation ) }
  , m_Offset{ offset }
{}

void Ember::GeometryAllocation::Write( uint32_t const offset, uint32_t const size, void const* data ) const
{
  m_Buffer.Write( m_Offset + offset, size, data );
}

uint32_t Ember::GeometryAllocation::GetOffsetInBytes() const
{
  return m_Offset;
}

D3D12_GPU_VIRTUAL_ADDRESS Ember::GeometryAllocation::GetGPUVirtualAddress() const
{
  return m_Buffer.GetGPUVirtualAddress() + m_Offset;
}

Ember::GeometryAllocation::GeometryAllocation( GeometryAllocation&& other ) noexcept
  : m_Buffer{ std::move( other.m_Buffer ) }
  , m_Block{ std::move( other.m_Block ) }
  , m_Allocation{ std::move( other.m_Allocation ) }
  , m_Offset{ other.m_Offset }
{
  other.m_Block = nullptr;
}

Ember::GeometryAllocation& Ember::GeometryAllocation::operator=( GeometryAllocation&& other ) noexcept
{
  if ( this == &other ) return *this;
  if ( m_Block ) m_Block->FreeAllocation( m_Allocation );

  m_Buffer      = std::move( other.m_Buffer );
  m_Block       = std::move( other.m_Block );
  m_Allocation  = std::move( other.m_Allocation );
  m_Offset      = other.m_Offset;

  other.m_Block = nullptr;
  return *this;
}

Ember::GeometryAllocation::~GeometryAllocation()
{
  if ( m_Block ) m_Block->FreeAllocation( m_Allocation );
}

Ember::GeometryManager::GeometryManager(
    Buffer unified_geometry_buffer, ComPtr<D3D12MA::VirtualBlock> geometry_buffer_allocator )
  : m_UnifiedGeometryBuffer{ std::move( unified_geometry_buffer ) }
  , m_GeometryBufferAllocator{ std::move( geometry_buffer_allocator ) }
{}

bool Ember::GeometryManager::Create(
    GeometryManager* geometry_manager, RenderDevice* render_device, uint32_t const total_ugb_size )
{
  Buffer                            ugb = render_device->CreateRawStorageBuffer( total_ugb_size );

  ComPtr<D3D12MA::VirtualBlock>     allocator;
  D3D12MA::VIRTUAL_BLOCK_DESC const block_desc{
    .Flags = D3D12MA::VIRTUAL_BLOCK_FLAG_NONE,
    .Size  = total_ugb_size,
  };
  ERR_FAIL_RET_F( CreateVirtualBlock( &block_desc, &allocator ) );

  new ( geometry_manager ) GeometryManager{ std::move( ugb ), std::move( allocator ) };

  return true;
}

Ember::GeometryAllocation Ember::GeometryManager::CreateGeometry(
    uint32_t const geometry_size, uint32_t const geometry_alignment )
{
  D3D12MA::VIRTUAL_ALLOCATION_DESC const desc{
    .Flags     = D3D12MA::VIRTUAL_ALLOCATION_FLAG_NONE,
    .Size      = geometry_size,
    .Alignment = geometry_alignment,
  };
  UINT64                     offset;
  D3D12MA::VirtualAllocation allocation;
  ERR_ABORT( m_GeometryBufferAllocator->Allocate( &desc, &allocation, &offset ) );

  return { m_UnifiedGeometryBuffer, m_GeometryBufferAllocator, allocation, ( uint32_t )offset };
}

Ember::SRVHandle Ember::GeometryManager::GetSRVHandle() const
{
  return m_UnifiedGeometryBuffer.GetSRVHandle();
}
