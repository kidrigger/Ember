#pragma once

#include "ResourceHandles.hpp"
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
    D3D12_GPU_VIRTUAL_ADDRESS   VirtualAddress; // Takes the Offset into account.
    uint32_t                    Offset;
    uint32_t                    Size;
    union
    {
      uint32_t    Stride;
      DXGI_FORMAT Format;
    };
  };

  // Mask for the generations in the allocation.
  // From 0 -> 4096.
  // Generation 0 is an 'invalid generation'.
  uint32_t static constexpr kGenerationMask   = 0xFFF00000;
  uint32_t static constexpr kGenerationOffset = 20;
  uint32_t static constexpr kIndexMask        = 0x000FFFFF;

  static_assert( kGenerationMask == ( 0xFFF << kGenerationOffset ) );

  FreeList     m_FreeList;
  BufferInner* m_Buffers;
  uint16_t*    m_Generations;
  uint32_t*    m_RefCount;
  uint32_t     m_Count;
  uint32_t     m_Capacity;

  // Freelist intrusively stores its pointers in the array.
  static_assert( sizeof( FreeList::Node ) <= sizeof( BufferInner ) );

  [[nodiscard]] BufferInner* AllocateInner();
  [[nodiscard]] BufferInner* FetchInner( Buffer buffer ) const;
  static void                AllocateBufferImpl(
                     D3D12MA::Allocator* allocator, uint32_t size, D3D12MA::Allocation** allocation, ID3D12Resource** resource );

public:
  BufferManager( BufferInner* buffers, uint16_t* generations, uint32_t* ref_count, uint32_t capacity );

  Buffer                   CreateVertexBuffer( D3D12MA::Allocator* allocator, uint32_t size, uint32_t stride );
  Buffer                   CreateIndexBuffer( D3D12MA::Allocator* allocator, uint32_t size, DXGI_FORMAT format );
  D3D12_VERTEX_BUFFER_VIEW GetVertexBufferView( Buffer buffer ) const;
  D3D12_INDEX_BUFFER_VIEW  GetIndexBufferView( Buffer buffer ) const;
  void                     WriteToBuffer( Buffer buffer, uint32_t offset, uint32_t size, void const* data ) const;

  static BufferManager     Create( uint32_t capacity );

  BufferManager( BufferManager const& other ) = delete;
  BufferManager( BufferManager&& other ) noexcept;
  BufferManager& operator=( BufferManager const& other ) = delete;
  BufferManager& operator=( BufferManager&& other ) noexcept;
  ~BufferManager();
};

} // namespace Ember
