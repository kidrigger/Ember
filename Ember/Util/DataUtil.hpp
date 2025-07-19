#pragma once

#include <ranges>

namespace Ember
{

uint32_t ByteSizeOf( std::ranges::contiguous_range auto& range )
{
  return ( uint32_t )( std::ranges::size( range ) * sizeof( std::ranges::range_value_t<decltype( range )> ) );
}

uint32_t CountOf( std::ranges::sized_range auto& range )
{
  return ( uint32_t )std::ranges::size( range );
}

} // namespace Ember
