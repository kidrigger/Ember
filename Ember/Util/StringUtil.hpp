#pragma once

#include <span>
#include <string_view>

#include "DataUtil.hpp"
#include "HelperUtils.hpp"

namespace Ember
{
inline void ToWideChar( std::span<wchar_t> const& buffer, std::string_view const& str )
{
  if ( str.empty() )
  {
    buffer[0] = L'\0';
    return;
  }
  ASSERT( not buffer.empty() );

  size_t     converted;
  auto const res = mbstowcs_s( &converted, DataOf( buffer ), CountOf( buffer ), DataOf( str ), _TRUNCATE );
  ASSERT( not res );
  ASSERT( converted != std::numeric_limits<size_t>::max() ); // Conversion error
  ASSERT( converted < buffer.size() );                       // Buffer size sufficient (includes null terminator)
}
} // namespace Ember
