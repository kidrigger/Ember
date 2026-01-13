#pragma once

#include <ranges>
#include <string_view>
#include <type_traits>

#include "HelperUtils.hpp"

using byte = unsigned char;

namespace Ember
{

// ReSharper disable once CppInconsistentNaming
constexpr std::_Ignore _{};

constexpr uint32_t     operator""_KiB( size_t const kibs )
{
  return ( uint32_t )kibs * ( 1 << 10 );
}

constexpr uint32_t operator""_MiB( size_t const mibs )
{
  return ( uint32_t )mibs * ( 1 << 20 );
}

constexpr size_t operator""_GiB( size_t const gibs )
{
  return gibs * ( 1 << 30 );
}

template <typename T, typename TFrom>
T CheckedCast( TFrom&& value )
  requires std::is_arithmetic_v<std::remove_cvref_t<T>> and std::is_arithmetic_v<std::remove_cvref_t<TFrom>>
{
  // If T is signed and TFrom is unsigned, this check is useless.
  // But it may upcast the signed to an overflowed positive and fail.
  if constexpr ( not std::is_signed_v<T> and std::is_signed_v<TFrom> ) ASSERT( std::numeric_limits<T>::min() <= value );
  ASSERT( value <= std::numeric_limits<T>::max() );
  return static_cast<T>( std::forward<TFrom>( value ) );
}

constexpr uint32_t U32ByteSizeOf( std::ranges::contiguous_range auto const& range )
{
  return CheckedCast<uint32_t>( std::ranges::size( range ) * sizeof( std::ranges::range_value_t<decltype( range )> ) );
}

constexpr size_t ByteSizeOf( std::ranges::contiguous_range auto const& range )
{
  return std::ranges::size( range ) * sizeof( std::ranges::range_value_t<decltype( range )> );
}

constexpr uint32_t StrideOf( std::ranges::contiguous_range auto& range )
{
  return CheckedCast<uint32_t>( sizeof( std::ranges::range_value_t<decltype( range )> ) );
}

constexpr uint32_t CountOf( std::ranges::sized_range auto& range )
{
  return CheckedCast<uint32_t>( std::ranges::size( range ) );
}

constexpr auto DataOf( std::ranges::contiguous_range auto& range )
{
  return std::ranges::data( range );
}

auto AsBytes( std::ranges::contiguous_range auto& range )
{
  uint32_t const size = U32ByteSizeOf( range );
  byte*          data = ( byte* )DataOf( range );
  return std::span( data, size );
}

template <typename T>
concept IsUnitObject = not std::ranges::range<T> and not std::is_pointer_v<T>;

class HashFnv1A
{
  uint64_t m_Value;

public:
  constexpr HashFnv1A( size_t const size, byte const* data ) // Offset
  {
    if ( size == 0 )
    {
      m_Value = 0;
      return;
    }

    uint64_t hash = 0xcbf29ce484222325; /* Offset */

    for ( size_t i = 0; i < size; ++i )
    {
      hash = hash ^ data[i];
      hash = hash * 0x00000100000001b3; /* Prime */
    }

    m_Value = hash;
  }

  constexpr HashFnv1A( IsUnitObject auto const& data ) : HashFnv1A( sizeof( data ), ( byte const* )&data )
  {}

  constexpr HashFnv1A( std::ranges::contiguous_range auto& range )
    : HashFnv1A( ByteSizeOf( range ), ( byte const* )DataOf( range ) )
  {}

  constexpr HashFnv1A& Combine( size_t const size, byte const* data )
  {
    for ( size_t i = 0; i < size; ++i )
    {
      m_Value = m_Value ^ data[i];
      m_Value = m_Value * 0x00000100000001b3; // Prime
    }
    return *this;
  }

  constexpr HashFnv1A& Combine( IsUnitObject auto const& data )
  {
    return Combine( sizeof( data ), ( byte const* )&data );
  }

  constexpr HashFnv1A& Combine( std::ranges::contiguous_range auto const& data )
  {
    return Combine( ByteSizeOf( data ), ( byte const* )DataOf( data ) );
  }

  constexpr HashFnv1A& operator<<( auto const& value )
    requires requires { Combine( value ); }
  {
    return Combine( value );
  }

  constexpr operator uint64_t() const
  {
    return m_Value;
  }
};
//
// constexpr uint64_t HashFnv1A( size_t const size, byte const* data )
//{
//  if ( size == 0 ) return 0;
//
//  uint64_t hash = 0xcbf29ce484222325; /* Offset */
//
//  for ( size_t i = 0; i < size; ++i )
//  {
//    hash = hash ^ data[i];
//    hash = hash * 0x00000100000001b3; /* Prime */
//  }
//
//  return hash;
//}
//
// constexpr uint64_t HashFnv1A( std::ranges::contiguous_range auto& range )
//{
//  byte const*  bytes = ( byte const* )DataOf( range );
//  size_t const size  = ByteSizeOf( range );
//  return HashFnv1A( size, bytes );
//}
//
// constexpr uint64_t HashFnv1A( IsUnitObject auto const& data )
//{
//  byte const*  bytes = ( byte const* )&data;
//  size_t const size  = sizeof( data );
//
//  return HashFnv1A( size, bytes );
//}
//
// constexpr uint64_t HashFnv1A( std::string_view const& data )
//{
//  byte const*  bytes = ( byte const* )data.data();
//  size_t const size  = data.size() * sizeof( char );
//
//  return HashFnv1A( size, bytes );
//}
//
// constexpr uint64_t HashFnv1ACombine( uint64_t hash, size_t const size, byte const* data )
//{
//  if ( size == 0 ) return hash;
//
//  for ( size_t i = 0; i < size; ++i )
//  {
//    hash = hash ^ data[i];
//    hash = hash * 0x00000100000001b3; /* Prime */
//  }
//
//  return hash;
//}
//
// constexpr uint64_t HashFnv1ACombine( uint64_t hash, IsUnitObject auto const& data )
//{
//  byte const*  bytes = ( byte const* )&data;
//  size_t const size  = sizeof( data );
//
//  return HashFnv1ACombine( hash, size, bytes );
//}

class StringID
{
  uint64_t m_Value;

public:
  constexpr StringID() : m_Value{ HashFnv1A( 0, nullptr ) }
  {}

  constexpr StringID( std::string_view const& str ) : m_Value{ HashFnv1A( str ) }
  {}

  constexpr StringID( char const* c_str, size_t const size ) : m_Value{ HashFnv1A( size, ( byte const* )c_str ) }
  {}

  StringID( char const* c_str ) : m_Value{ HashFnv1A( c_str ? strlen( c_str ) : 0, ( byte const* )c_str ) }
  {}

  [[nodiscard]] constexpr uint64_t GetValue() const
  {
    return m_Value;
  }

  constexpr auto operator<=>( StringID const& ) const = default;
};

constexpr StringID operator""_id( char const* data, size_t const size )
{
  return StringID{ data, size };
}

} // namespace Ember
