#include "BufferManager.hpp"

#include "Util/HelperUtils.hpp"

Ember::BufferManager::BufferInner* Ember::BufferManager::AllocateInner()
{
  ASSERT( not m_FreeList.Empty() );

  void* alloc = m_FreeList.PopFront();
  return new ( alloc ) BufferInner{};
}

Ember::BufferManager::BufferInner* Ember::BufferManager::FetchInner( Buffer const buffer ) const
{
  uint32_t const index = buffer.m_Index & kIndexMask;
  ASSERT( index <= m_Capacity );

  uint32_t const generation = ( buffer.m_Index & kGenerationMask ) >> kGenerationOffset;
  ASSERT( generation == m_Generations[index] );

  return m_Buffers + index;
}

Ember::BufferManager::BufferManager(
    BufferInner* buffers, uint16_t* generations, uint32_t* ref_count, uint32_t const capacity )
  : m_Buffers{ buffers }, m_Generations{ generations }, m_RefCount{ ref_count }, m_Count{ 0 }, m_Capacity{ capacity }
{
  memset( generations, 0, m_Capacity * sizeof *m_Generations );
  for ( uint32_t i = 0; i < capacity; ++i )
  {
    m_FreeList.PushBack( ( FreeList::Node* )( m_Buffers + i ) );
  }

  // handle = 0 is invalid, so generation[0] is set to 1.
  m_Generations[0] = 1;
}

void Ember::BufferManager::AllocateBufferImpl(
    D3D12MA::Allocator* allocator, uint32_t const size, D3D12MA::Allocation** allocation, ID3D12Resource** resource )
{
  D3D12MA::ALLOCATION_DESC constexpr allocation_desc = {
    .Flags    = D3D12MA::ALLOCATION_FLAG_NONE,
    .HeapType = D3D12_HEAP_TYPE_GPU_UPLOAD,
  };

  CD3DX12_RESOURCE_DESC1 const buffer_desc = CD3DX12_RESOURCE_DESC1::Buffer( size );

  ERR_ABORT( allocator->CreateResource2(
      &allocation_desc, &buffer_desc, D3D12_RESOURCE_STATE_COMMON, nullptr, allocation, IID_PPV_ARGS( resource ) ) );
}

Ember::Buffer Ember::BufferManager::CreateVertexBuffer(
    D3D12MA::Allocator* allocator, uint32_t const size, uint32_t const stride )
{
  BufferInner*   inner = AllocateInner();
  uint64_t const index = inner - m_Buffers;

  ASSERT( index <= kIndexMask );

  AllocateBufferImpl( allocator, size, &inner->Allocation, &inner->Buffer );

  D3D12_GPU_VIRTUAL_ADDRESS const address = inner->Buffer->GetGPUVirtualAddress();

  inner->VirtualAddress                   = address;
  inner->Offset                           = 0;
  inner->Size                             = size;
  inner->Stride                           = stride;

  m_RefCount[index]                       = 1;
  uint32_t const handle                   = ( m_Generations[index] << kGenerationOffset ) | ( uint32_t )index;

  return Buffer( handle );
}

Ember::Buffer Ember::BufferManager::CreateIndexBuffer(
    D3D12MA::Allocator* allocator, uint32_t const size, DXGI_FORMAT const format )
{
  BufferInner*   inner = AllocateInner();
  uint64_t const index = inner - m_Buffers;

  ASSERT( index <= kIndexMask );

  AllocateBufferImpl( allocator, size, &inner->Allocation, &inner->Buffer );

  D3D12_GPU_VIRTUAL_ADDRESS const address = inner->Buffer->GetGPUVirtualAddress();

  inner->VirtualAddress                   = address;
  inner->Offset                           = 0;
  inner->Size                             = size;
  inner->Format                           = format;

  m_RefCount[index]                       = 1;
  uint32_t const handle                   = ( m_Generations[index] << kGenerationOffset ) | ( uint32_t )index;

  return Buffer( handle );
}

D3D12_VERTEX_BUFFER_VIEW Ember::BufferManager::GetVertexBufferView( Buffer const buffer ) const
{
  BufferInner const* inner = FetchInner( buffer );

  return {
    .BufferLocation = inner->VirtualAddress,
    .SizeInBytes    = inner->Size,
    .StrideInBytes  = inner->Stride,
  };
}

D3D12_INDEX_BUFFER_VIEW Ember::BufferManager::GetIndexBufferView( Buffer const buffer ) const
{
  BufferInner const* inner = FetchInner( buffer );

  return {
    .BufferLocation = inner->VirtualAddress,
    .SizeInBytes    = inner->Size,
    .Format         = inner->Format,
  };
}

void Ember::BufferManager::WriteToBuffer(
    Buffer const buffer, uint32_t const offset, uint32_t const size, void const* data ) const
{
  BufferInner const* inner = FetchInner( buffer );

  ASSERT( offset + size <= inner->Size );

  uint32_t const    absolute_offset  = inner->Offset + offset;
  D3D12_RANGE const empty_read_range = { 0, 0 };
  D3D12_RANGE const write_range      = { absolute_offset, absolute_offset + size };

  byte*             mapped;
  ERR_ABORT( inner->Buffer->Map( 0, &empty_read_range, ( void** )&mapped ) );

  memcpy( mapped + absolute_offset, data, size );

  inner->Buffer->Unmap( 0, &write_range );
}

Ember::BufferManager Ember::BufferManager::Create( uint32_t const capacity )
{
  BufferInner* buffers     = new BufferInner[capacity];
  uint16_t*    generations = new uint16_t[capacity];
  uint32_t*    ref_count   = new uint32_t[capacity];

  return BufferManager{ buffers, generations, ref_count, capacity };
}

Ember::BufferManager::BufferManager( BufferManager&& other ) noexcept
  : m_FreeList{ std::move( other.m_FreeList ) }
  , m_Buffers{ other.m_Buffers }
  , m_Generations{ other.m_Generations }
  , m_RefCount{ other.m_RefCount }
  , m_Count{ other.m_Count }
  , m_Capacity{ other.m_Capacity }
{
  other.m_Buffers     = nullptr;
  other.m_Generations = nullptr;
  other.m_RefCount    = nullptr;
  other.m_Capacity    = 0;
}

Ember::BufferManager& Ember::BufferManager::operator=( BufferManager&& other ) noexcept
{
  if ( this == &other ) return *this;
  std::swap( m_FreeList, other.m_FreeList );
  std::swap( m_Buffers, other.m_Buffers );
  std::swap( m_Generations, other.m_Generations );
  std::swap( m_RefCount, other.m_RefCount );
  std::swap( m_Count, other.m_Count );
  std::swap( m_Capacity, other.m_Capacity );
  return *this;
}

Ember::BufferManager::~BufferManager()
{
  while ( not m_FreeList.Empty() )
  {
    void* alloc = m_FreeList.PopFront();
    new ( alloc ) BufferInner{};
  }

  delete[] m_Buffers;
  delete[] m_Generations;
  delete[] m_RefCount;
}
