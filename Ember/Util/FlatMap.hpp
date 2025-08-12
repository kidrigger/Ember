#pragma once

#include <algorithm>
#include <vector>

namespace Ember
{

template <std::totally_ordered TKey, typename TValue>
class FlatMap
{
  std::vector<TKey>   m_Keys;
  std::vector<TValue> m_Values;

public:
  bool Contains( TKey const& key )
  {
    return std::ranges::binary_search( m_Keys, key );
  }

  TValue const& Get( TKey const& key ) const
  {
    auto it = std::ranges::lower_bound( m_Keys, key );
    ASSERT( it != m_Keys.end() );

    ptrdiff_t offset = it - m_Keys.begin();
    return m_Values[offset];
  }

  TValue& Get( TKey const& key )
  {
    auto it = std::ranges::lower_bound( m_Keys, key );
    ASSERT( it != m_Keys.end() );

    ptrdiff_t offset = it - m_Keys.begin();
    return m_Values[offset];
  }

  TValue& Put( TKey const& key, TValue&& value )
  {
    ASSERT( not Contains( key ) );

    auto key_it = std::ranges::lower_bound( key );
    m_Keys.insert( key_it, key );
    ptrdiff_t offset   = key_it - m_Keys.begin();
    auto      value_it = m_Values.begin() + offset;

    return m_Values.emplace( value, std::forward<TValue>( value ) );
  }

  TValue& operator[]( TKey const& key )
    requires std::is_default_constructible_v<TValue>
  {
    if ( Contains( key ) )
    {
      return Get( key );
    }

    return Put( key, {} );
  }

  TValue& operator[]( TKey const& key )
    requires not std::is_default_constructible_v<TValue>
  {
    ASSERT( Contains( key ) );
    return Get( key );
  }

  TValue const& operator[]( TKey const& key )
  {
    return Get( key );
  }
};

} // namespace Ember
