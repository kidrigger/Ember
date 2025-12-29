#pragma once

#include <mutex>
#include <vector>

#include <Util/Runtime.hpp>

namespace Ember
{
struct Chunk
{
  byte*              Data;
  uint8_t            Remaining;
  uint8_t            FreeHead;

  [[nodiscard]] bool CanAllocate() const
  {
    return Remaining > 0;
  }

  [[nodiscard]] bool ContainsAllocation( byte const* ptr, size_t const aligned_size ) const
  {
    return Data <= ptr and ptr <= Data + aligned_size * 255;
  }

  [[nodiscard]] bool IsEmpty() const
  {
    return Remaining == 255;
  }

  void  Init( size_t aligned_size );
  void  Reset( size_t aligned_size );
  void  Destroy();

  byte* Allocate( size_t aligned_size );
  void  Deallocate( byte* allocation, size_t aligned_size );
};

template <typename T>
concept RefCounted = requires( T a ) {
  { a.AddRef() } -> std::convertible_to<uint32_t>;
  { a.Release() } -> std::convertible_to<uint32_t>;
  { a.GetRefCount() } -> std::convertible_to<uint32_t>;
};

class ObjectPoolBase
{
  std::vector<Chunk> m_Chunks;
  Chunk*             m_LastAlloc{ nullptr };
  Chunk*             m_LastFreed{ nullptr };

protected:
  byte* Allocate( size_t aligned_size );
  void  Deallocate( byte* allocation, size_t aligned_size );

public:
  void Shrink();

  ObjectPoolBase()                              = default;
  ObjectPoolBase( ObjectPoolBase const& other ) = delete;
  ObjectPoolBase( ObjectPoolBase&& other ) noexcept;
  ObjectPoolBase& operator=( ObjectPoolBase const& other ) = delete;
  ObjectPoolBase& operator=( ObjectPoolBase&& other ) noexcept;
  ~ObjectPoolBase();
};

template <typename T>
class ObjectPool : public ObjectPoolBase
{
  using Super = ObjectPoolBase;
  using Type  = T;
  constexpr static size_t AlignedSize( size_t const size, size_t const alignment )
  {
    size_t const offset = size % alignment;
    if ( offset == 0 ) return size;
    return size + ( alignment - offset );
  }
  constexpr static size_t kAlignedSize = AlignedSize( sizeof( Type ), alignof( Type ) );

  std::mutex              m_ThreadLock;

public:
  Type* Allocate()
  {
    auto lock_guard = std::lock_guard( m_ThreadLock );
    return ( Type* )Super::Allocate( kAlignedSize );
  }

  void Deallocate( Type* ptr )
  {
    auto lock_guard = std::lock_guard( m_ThreadLock );
    Super::Deallocate( ( byte* )ptr, kAlignedSize );
  }

  Type* Construct()
    requires std::is_default_constructible_v<Type>
  {
    Type* mem = Allocate();
    return new ( mem ) Type{};
  }

  Type* Construct( auto&&... args )
    requires std::constructible_from<Type, decltype( args )...>
  {
    Type* mem = Allocate();
    return new ( mem ) Type{ std::forward<decltype( args )>( args )... };
  }

  void Destroy( Type* object )
    requires not RefCounted<Type>
  {
    if ( object == nullptr ) return;

    object->~Type();
    Deallocate( object );
  }

  void Destroy( Type* object )
    requires RefCounted<Type>
  {
    if ( object == nullptr ) return;

    if ( object->Release() > 0 ) return;

    object->~Type();
    Deallocate( object );
  }

  Type* Copy( Type* object )
    requires RefCounted<Type>
  {
    ASSERT( object->GetRefCount() > 0 );
    object->AddRef();
    return object;
  }
};

} // namespace Ember
