#pragma once

#include <algorithm>
#include <vector>
#include "Util/HelperUtils.hpp"

namespace Ember
{

template <std::totally_ordered TKey, typename TValue>
class FlatMap
{
  using KeyIter        = typename std::vector<TKey>::const_iterator;
  using ValueIter      = typename std::vector<TValue>::iterator;
  using ValueConstIter = typename std::vector<TValue>::const_iterator;

  std::vector<TKey>   m_Keys;
  std::vector<TValue> m_Values;

public:
  class Iterator
  {
    KeyIter   m_KeyIter;
    ValueIter m_ValueIter;

    struct KeyValueProxy
    {
      std::pair<TKey const&, TValue&>  Val;

      std::pair<TKey const&, TValue&>* operator->()
      {
        return &Val;
      }
    };

    struct KeyValueConstProxy
    {
      std::pair<TKey const&, TValue const&>  Val;

      std::pair<TKey const&, TValue const&>* operator->()
      {
        return &Val;
      }
    };

  public:
    Iterator( KeyIter key_iter, ValueIter value_iter )
      : m_KeyIter{ std::move( key_iter ) }, m_ValueIter{ std::move( value_iter ) }
    {}

    std::pair<TKey const&, TValue&> operator*()
    {
      return { *m_KeyIter, *m_ValueIter };
    }

    Iterator operator+( ptrdiff_t const offset ) const
    {
      return { m_KeyIter + offset, m_ValueIter + offset };
    }

    Iterator& operator++()
    {
      ++m_KeyIter;
      ++m_ValueIter;
      return *this;
    }

    Iterator& operator--()
    {
      --m_KeyIter;
      --m_ValueIter;
      return *this;
    }

    KeyValueProxy operator->()
    {
      return KeyValueProxy{
        { *m_KeyIter, *m_ValueIter }
      };
    }

    KeyValueConstProxy operator->() const
    {
      return KeyValueConstProxy{
        { *m_KeyIter, *m_ValueIter }
      };
    }

    auto                  operator<=>( Iterator const& ) const = default;

    [[nodiscard]] KeyIter GetKeyIter() const
    {
      return m_KeyIter;
    }
    [[nodiscard]] ValueIter GetValueIter() const
    {
      return m_ValueIter;
    }
  };

  class ConstIterator
  {
    KeyIter        m_KeyIter;
    ValueConstIter m_ValueIter;

    struct KeyValueProxy
    {
      std::pair<TKey const&, TValue const&>        Val;

      std::pair<TKey const&, TValue const&> const* operator->() const
      {
        return &Val;
      }
    };

  public:
    ConstIterator( KeyIter key_iter, ValueConstIter value_iter )
      : m_KeyIter{ std::move( key_iter ) }, m_ValueIter{ std::move( value_iter ) }
    {}

    std::pair<TKey const&, TValue const&> operator*() const
    {
      return { *m_KeyIter, *m_ValueIter };
    }

    ConstIterator operator+( ptrdiff_t const offset ) const
    {
      return { m_KeyIter + offset, m_ValueIter + offset };
    }

    ConstIterator& operator++()
    {
      ++m_KeyIter;
      ++m_ValueIter;
      return *this;
    }

    ConstIterator& operator--()
    {
      --m_KeyIter;
      --m_ValueIter;
      return *this;
    }

    KeyValueProxy operator->() const
    {
      return KeyValueProxy{
        { *m_KeyIter, *m_ValueIter }
      };
    }

    auto                  operator<=>( ConstIterator const& ) const = default;

    [[nodiscard]] KeyIter GetKeyIter() const
    {
      return m_KeyIter;
    }
    [[nodiscard]] ValueConstIter GetValueIter() const
    {
      return m_ValueIter;
    }
  };

  FlatMap() = default;
  FlatMap( std::vector<TKey> keys, std::vector<TValue> values )
    : m_Keys{ std::move( keys ) }, m_Values{ std::move( values ) }
  {
    ASSERT( keys.size() == values.size() );
  }

  bool Contains( TKey const& key )
  {
    return std::ranges::binary_search( m_Keys, key );
  }

  Iterator Find( TKey const& key )
  {
    auto key_it = std::ranges::lower_bound( m_Keys, key );
    if ( key_it == m_Keys.end() or *key_it != key ) return end();

    ptrdiff_t offset = key_it - m_Keys.begin();
    return { key_it, m_Values.begin() + offset };
  }

  ConstIterator Find( TKey const& key ) const
  {
    auto key_it = std::ranges::lower_bound( m_Keys, key );
    if ( key_it == m_Keys.end() or *key_it != key ) return end();

    ptrdiff_t offset = key_it - m_Keys.begin();
    return { key_it, m_Values.begin() + offset };
  }

  Iterator LowerBound( TKey const& key )
  {
    auto      key_it = std::ranges::lower_bound( m_Keys, key );
    ptrdiff_t offset = key_it - m_Keys.begin();
    return { key_it, m_Values.begin() + offset };
  }

  ConstIterator LowerBound( TKey const& key ) const
  {
    auto      key_it = std::ranges::lower_bound( m_Keys, key );
    ptrdiff_t offset = key_it - m_Keys.begin();
    return { key_it, m_Values.begin() + offset };
  }

  TValue const& Get( TKey const& key ) const
  {
    KeyIter it = std::ranges::lower_bound( m_Keys, key );
    ASSERT( it != m_Keys.end() );

    ptrdiff_t offset = it - m_Keys.begin();
    return m_Values[offset];
  }

  TValue& Get( TKey const& key )
  {
    KeyIter it = std::ranges::lower_bound( m_Keys, key );
    ASSERT( it != m_Keys.end() );

    ptrdiff_t offset = it - m_Keys.begin();
    return m_Values[offset];
  }

  TValue& Put( TKey const& key, TValue&& value )
  {
    ASSERT( not Contains( key ) );

    KeyIter key_it     = std::ranges::lower_bound( m_Keys, key );

    key_it             = m_Keys.insert( key_it, key );
    ptrdiff_t offset   = key_it - m_Keys.begin();
    ValueIter value_it = m_Values.begin() + offset;

    return *m_Values.emplace( value_it, std::forward<TValue>( value ) );
  }

  TValue& Put( TKey const& key, TValue const& value )
  {
    ASSERT( not Contains( key ) );

    KeyIter key_it     = std::ranges::lower_bound( m_Keys, key );

    key_it             = m_Keys.insert( key_it, key );
    ptrdiff_t offset   = key_it - m_Keys.begin();
    ValueIter value_it = m_Values.begin() + offset;

    return *m_Values.emplace( value_it, value );
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

  void Erase( Iterator const& iter )
  {
    if ( iter == end() ) return;
    m_Keys.erase( iter.GetKeyIter() );
    m_Values.erase( iter.GetValueIter() );
  }

  void Erase( TKey const& key )
  {
    auto it = Find( key );
    if ( it != end() ) Erase( it );
  }

  template <std::predicate<TKey const&, TValue const&> TPredicate>
  void EraseIf( TPredicate&& predicate )
  {
    for ( size_t i = m_Keys.size(); i > 0; --i )
    {
      size_t index = i - 1;
      if ( std::invoke( std::forward<TPredicate>( predicate ), m_Keys[index], m_Values[index] ) )
      {
        m_Keys.erase( m_Keys.begin() + index );
        m_Values.erase( m_Values.begin() + index );
      }
    }
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

  [[nodiscard]] size_t Size() const
  {
    return m_Keys.size();
  }

  Iterator begin()
  {
    return { m_Keys.begin(), m_Values.begin() };
  }

  Iterator end()
  {
    return { m_Keys.end(), m_Values.end() };
  }

  ConstIterator begin() const
  {
    return { m_Keys.begin(), m_Values.begin() };
  }

  ConstIterator end() const
  {
    return { m_Keys.end(), m_Values.end() };
  }

  std::vector<TKey> const& Keys() const
  {
    return m_Keys;
  }

  std::vector<TValue>& Values()
  {
    return m_Values;
  }

  std::vector<TValue> const& Values() const
  {
    return m_Values;
  }

  void Clear()
  {
    m_Keys.clear();
    m_Values.clear();
  }
};

} // namespace Ember
