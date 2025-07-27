#include "ObjectPool.hpp"

#include "Util/HelperUtils.hpp"

void Ember::Chunk::Init( size_t const aligned_size )
{
  ASSERT( aligned_size > 0 );

  size_t const byte_size = aligned_size * 255;
  ASSERT( byte_size / 255 == aligned_size ); // Overflow

  Data       = new byte[byte_size];
  Remaining  = 255;
  FreeHead   = 0;
  byte* iter = Data;
  for ( int i = 1; i < Remaining; ++i )
  {
    *iter  = ( uint8_t )i;
    iter  += aligned_size;
  }
  *iter = 0;
}

void Ember::Chunk::Reset( size_t const aligned_size )
{
  size_t const byte_size = aligned_size * 255;
  memset( Data, 0, byte_size );
  Remaining  = 255;
  FreeHead   = 0;
  byte* iter = Data;
  for ( int i = 1; i < Remaining; ++i )
  {
    *iter  = ( uint8_t )i;
    iter  += aligned_size;
  }
  *iter = 0;
}

void Ember::Chunk::Destroy()
{
  delete[] Data;
  Data      = nullptr;
  Remaining = 255;
  FreeHead  = 0;
}

byte* Ember::Chunk::Allocate( size_t const aligned_size )
{
  ASSERT( CanAllocate() );

  uint8_t const free_head = FreeHead;
  byte*         block     = Data + aligned_size * free_head;
  FreeHead                = *block;

  Remaining--;

  return block;
}

void Ember::Chunk::Deallocate( byte* allocation, size_t const aligned_size )
{
  ptrdiff_t const byte_offset = ( allocation - Data );
  ASSERT( byte_offset >= 0 );
  ASSERT( byte_offset % aligned_size == 0 );

  size_t const index = byte_offset / aligned_size;

  ASSERT( index <= 0xFF ); // Must be in range

  uint8_t const free_head = ( uint8_t )index;
  *allocation             = FreeHead;
  FreeHead                = free_head;

  ++Remaining;
}

byte* Ember::ObjectPoolBase::Allocate( size_t const aligned_size )
{
  if ( m_LastAlloc == nullptr or not m_LastAlloc->CanAllocate() )
  {
    // We have a free one readily available.
    if ( m_LastFreed != nullptr and m_LastFreed->CanAllocate() )
    {
      m_LastAlloc = m_LastFreed;
    }
    // Search or add
    else
    {
      size_t       i;
      size_t const len = m_Chunks.size();
      for ( i = 0; i < len; ++i )
      {
        if ( m_Chunks[i].CanAllocate() )
        {
          m_LastAlloc = &m_Chunks[i];
          break;
        }
      }
      if ( i == len )
      {
        m_LastAlloc = &m_Chunks.emplace_back();
        m_LastAlloc->Init( aligned_size );
        m_LastFreed = m_LastAlloc; // Pointer invalidated, must be reset.
      }
    }
  }
  return m_LastAlloc->Allocate( aligned_size );
}

void Ember::ObjectPoolBase::Deallocate( byte* allocation, size_t const aligned_size )
{
  if ( m_LastFreed != nullptr and m_LastFreed->ContainsAllocation( allocation, aligned_size ) )
  {
    m_LastFreed->Deallocate( allocation, aligned_size );
    return;
  }

  size_t const len = m_Chunks.size();
  for ( size_t i = 0; i < len; ++i )
  {
    if ( m_Chunks[i].ContainsAllocation( allocation, aligned_size ) )
    {
      m_LastFreed = &m_Chunks[i];
      m_LastFreed->Deallocate( allocation, aligned_size );
      return;
    }
  }
  ASSERT( false );
}

void Ember::ObjectPoolBase::Shrink()
{
  size_t const len = m_Chunks.size();
  for ( size_t i = 0; i < len; ++i )
  {
    Chunk& chunk = m_Chunks[len - i - 1];
    if ( chunk.IsEmpty() )
    {
      chunk.Destroy();
      std::swap( chunk, m_Chunks.back() );
      m_Chunks.pop_back();
    }
  }
}

Ember::ObjectPoolBase::ObjectPoolBase( ObjectPoolBase&& other ) noexcept
  : m_Chunks{ std::move( other.m_Chunks ) }, m_LastAlloc{ other.m_LastAlloc }, m_LastFreed{ other.m_LastFreed }
{
  other.m_LastAlloc = nullptr;
  other.m_LastFreed = nullptr;
}

Ember::ObjectPoolBase& Ember::ObjectPoolBase::operator=( ObjectPoolBase&& other ) noexcept
{
  if ( this == &other ) return *this;
  std::swap( m_Chunks, other.m_Chunks );
  std::swap( m_LastAlloc, other.m_LastAlloc );
  std::swap( m_LastFreed, other.m_LastFreed );
  return *this;
}

Ember::ObjectPoolBase::~ObjectPoolBase()
{
  for ( auto& chunk : m_Chunks )
  {
    chunk.Destroy();
  }
}
