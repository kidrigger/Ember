#pragma once

#include "ResourceHandles.h"
#include "Util/DirectXHeaders.hpp"
#include "Util/FreeList.hpp"
#include "Util/Runtime.hpp"

namespace Ember
{

class BufferManager
{
  struct BufferInner
  {
    ComPtr<ID3D12Resource>      Buffer;
    ComPtr<D3D12MA::Allocation> Allocation;
  };

  // Mask for the generations in the allocation.
  // From 0 -> 4096.
  // Generation 0 is an 'invalid generation'.
  uint32_t static constexpr GENERATION_MASK   = 0xFFF00000;
  uint32_t static constexpr GENERATION_OFFSET = 20;
  uint32_t static constexpr INDEX_MASK        = 0x000FFFFF;

  static_assert( GENERATION_MASK == ( 0xFFF << GENERATION_OFFSET ) );

  FreeList     m_FreeList;
  BufferInner* m_Buffers;
  uint16_t*    m_Generations;
  uint32_t*    m_RefCount;
  uint32_t     m_Count;
  uint32_t     m_Capacity;

  // Freelist intrusively stores its pointers in the array.
  static_assert( sizeof( FreeList::Node ) >= sizeof( BufferInner ) );

public:
  BufferManager( BufferInner* buffers, uint16_t* generations, uint32_t* ref_count, uint32_t capacity );

  Buffer               CreateUniformBuffer( D3D12MA::Allocator* allocator, size_t size );

  static BufferManager Create( uint32_t const capacity );
  void                 Destroy();
};

} // namespace Ember
