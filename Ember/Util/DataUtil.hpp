#pragma once

#include <ranges>

namespace Ember
{

size_t ByteSizeOf( std::ranges::contiguous_range auto& range )
{
  return std::ranges::size( range ) * sizeof( std::ranges::range_value_t<decltype( range )> );
}

size_t CountOf( std::ranges::sized_range auto& range )
{
  return std::ranges::size( range );
}

uint32_t CountOf( std::ranges::sized_range auto& range )
{
  return std::ranges::size( range );
}

} // namespace Ember
