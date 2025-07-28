#pragma once

#include <ranges>

namespace Ember
{

constexpr uint32_t ByteSizeOf( std::ranges::contiguous_range auto& range )
{
  return ( uint32_t )( std::ranges::size( range ) * sizeof( std::ranges::range_value_t<decltype( range )> ) );
}

constexpr uint32_t CountOf( std::ranges::sized_range auto& range )
{
  return ( uint32_t )std::ranges::size( range );
}

constexpr auto DataOf( std::ranges::contiguous_range auto& range )
{
  return std::ranges::data( range );
}

constexpr uint64_t HashFnv1A( size_t const size, byte const* data )
{
  uint64_t hash = 0xcbf29ce484222325; /* Offset */

  for ( size_t i = 0; i < size; ++i )
  {
    hash = hash ^ data[i];
    hash = hash * 0x00000100000001b3; /* Prime */
  }

  return hash;
}

constexpr uint64_t HashFnv1A( auto& data )
  requires not std::ranges::range<decltype( data )> and not std::is_pointer_v<decltype( data )>
{
  byte const*  bytes = ( byte* )&data;
  size_t const size  = sizeof( data );

  return HashFnv1A( size, bytes );
}

constexpr uint64_t HashFnv1A( std::string& data )
{
  byte const*  bytes = ( byte* )data.data();
  size_t const size  = data.size() * sizeof( char );

  return HashFnv1A( size, bytes );
}

} // namespace Ember
