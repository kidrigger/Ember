#include "BufferManager.hpp"

#include "Util/HelperUtils.hpp"

Ember::BufferManager::BufferManager(
    BufferInner* buffers, uint16_t* generations, uint32_t* ref_count, uint32_t const capacity )
  : m_Buffers{ buffers }, m_Generations{ generations }, m_RefCount{ ref_count }, m_Count{ 0 }, m_Capacity{ capacity }
{
  for ( uint32_t i = 0; i < capacity; ++i )
  {
    m_FreeList.PushBack( ( FreeList::Node* )( m_Buffers + i ) );
  }
}

Ember::Buffer Ember::BufferManager::CreateUniformBuffer( D3D12MA::Allocator* allocator, size_t const size )
{
  ASSERT( not m_FreeList.Empty() );

  void*          alloc = m_FreeList.PopFront();
  BufferInner*   inner = new ( alloc ) BufferInner{};
  uint64_t const index = inner - m_Buffers;

  ASSERT( index <= kIndexMask );

  D3D12MA::ALLOCATION_DESC constexpr allocation_desc = {
    .Flags    = D3D12MA::ALLOCATION_FLAG_NONE,
    .HeapType = D3D12_HEAP_TYPE_GPU_UPLOAD,
  };

  CD3DX12_RESOURCE_DESC1 const buffer_desc = CD3DX12_RESOURCE_DESC1::Buffer( size );

  ERR_ABORT( allocator->CreateResource2(
      &allocation_desc,
      &buffer_desc,
      D3D12_RESOURCE_STATE_COMMON,
      nullptr,
      &inner->Allocation,
      IID_PPV_ARGS( &inner->Buffer ) ) );

  m_RefCount[index]     = 1;
  uint32_t const handle = ( m_Generations[index] << kGenerationOffset ) | ( ( uint32_t )index & kIndexMask );

  return Buffer( handle );
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
