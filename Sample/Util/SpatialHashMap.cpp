#include "SpatialHashMap.hpp"

#include <ranges>

#include "Util/DataUtil.hpp"
#include "Util/HelperUtils.hpp"

Ember::SpatialHashMap::TableData::TableData( size_t const table_size )
  : Control( table_size, 0 ), Cells( table_size ), Index( table_size, 0 ), ProbeSeqLen( table_size, 0 )
{
  ASSERT( IsPowerOfTwo( table_size ) );
}

size_t Ember::SpatialHashMap::TableData::GetSize() const noexcept
{
  return Control.size();
}

uint32_t Ember::SpatialHashMap::Murmur3Hash( uint32_t k )
{
  k ^= k >> 16;
  k *= 0x85ebca6b;
  k ^= k >> 13;
  k *= 0xc2b2ae35;
  k ^= k >> 16;
  return k;
}

uint32_t Ember::SpatialHashMap::XxHash32( uint32_t const p )
{
  constexpr uint32_t prime32_2 = 2246822519U;
  constexpr uint32_t prime32_3 = 3266489917U;
  constexpr uint32_t prime32_4 = 668265263U;
  constexpr uint32_t prime32_5 = 374761393U;
  uint32_t           h32       = p + prime32_5;
  h32                          = prime32_4 * ( ( h32 << 17 ) | ( h32 >> ( 32 - 17 ) ) );
  h32                          = prime32_2 * ( h32 ^ ( h32 >> 15 ) );
  h32                          = prime32_3 * ( h32 ^ ( h32 >> 13 ) );
  return h32 ^ ( h32 >> 16 );
}

uint32_t Ember::SpatialHashMap::HashSlot( DirectX::XMINT3 const cell ) const
{
  auto const cell_size_bits = std::bit_cast<uint32_t>( m_CellSize );
  return XxHash32( cell_size_bits + XxHash32( cell.x + XxHash32( cell.y + XxHash32( cell.z ) ) ) );
}

uint32_t Ember::SpatialHashMap::HashControl( DirectX::XMINT3 const cell ) const
{
  auto const cell_size_bits = std::bit_cast<uint32_t>( m_CellSize );
  return kUsedBit |
         Murmur3Hash( cell_size_bits + Murmur3Hash( cell.x + Murmur3Hash( cell.y + Murmur3Hash( cell.z ) ) ) );
}

bool Ember::SpatialHashMap::IsUsed( uint32_t const value )
{
  return value & kUsedBit;
}

bool Ember::SpatialHashMap::IsDeleted( uint32_t const value )
{
  return value == kDeleted;
}

bool Ember::SpatialHashMap::Rehash()
{
  auto const old_data = std::move( m_TableData );
  m_TableData         = TableData{ old_data.GetSize() * 2 };

  for ( size_t i = 0; i < old_data.GetSize(); i++ )
  {
    if ( not IsUsed( old_data.Control[i] ) ) continue;

    auto const cell = old_data.Cells[i];
    if ( not PutCell( cell, old_data.Index[i] ) ) return false;
  }

  return true;
}

DirectX::XMINT3 Ember::SpatialHashMap::PositionToCell( DirectX::XMFLOAT3 const position ) const
{
  return {
    ( int32_t )std::floor( position.x / m_CellSize ),
    ( int32_t )std::floor( position.y / m_CellSize ),
    ( int32_t )std::floor( position.z / m_CellSize ),
  };
}

std::optional<uint32_t> Ember::SpatialHashMap::FetchSlot( DirectX::XMINT3 const cell ) const
{
  auto const hash_value = HashSlot( cell );
  auto const hash_check = HashControl( cell );
  auto const table_size = m_TableData.Control.size();

  uint32_t   slot       = hash_value & ( table_size - 1 );
  for ( uint32_t i = 0; i < kMaxProbeSeqLen; i++ )
  {
    uint32_t const check = m_TableData.Control[slot];

    // If unused, we end probing.
    // Don't have the key in the table.
    if ( not IsUsed( check ) and not IsDeleted( check ) ) return {};

    if ( check == hash_check )
    {
      ASSERT_M( cell == m_TableData.Cells[slot], "Validation failure." );
      return slot;
    }
    slot = ( slot + 1 ) & ( table_size - 1 );
  }
  return {};
}

bool Ember::SpatialHashMap::PutCell( DirectX::XMINT3 cell, uint32_t index )
{
  auto const              hash_value     = HashSlot( cell );
  auto                    hash_check     = HashControl( cell );

  auto const              table_size     = m_TableData.Control.size();

  std::optional<uint32_t> fetched_slot   = {};
  uint32_t                slot           = hash_value & ( table_size - 1 );
  uint8_t                 probe_seq_iter = 0;

  // We are seeking for the slot of original key
  while ( probe_seq_iter < kMaxProbeSeqLen )
  {
    uint32_t const check = m_TableData.Control[slot];

    // Slot unused, or belongs to this key.
    if ( not IsUsed( check ) or IsDeleted( check ) or check == hash_check )
    {
      m_TableData.Control[slot]     = hash_check;
      m_TableData.Cells[slot]       = cell;
      m_TableData.Index[slot]       = index;
      m_TableData.ProbeSeqLen[slot] = probe_seq_iter;

      m_Size++;
      return true;
    }

    // Robin hood PSL.
    // If the current slot has shorter probe sequence length, swap with iter.
    if ( m_TableData.ProbeSeqLen[slot] < probe_seq_iter )
    {
      std::swap( m_TableData.Control[slot], hash_check );
      std::swap( m_TableData.Cells[slot], cell );
      std::swap( m_TableData.Index[slot], index );
      std::swap( m_TableData.ProbeSeqLen[slot], probe_seq_iter );
      if ( not fetched_slot )
      {
        // If this is where we place the input, this is the slot.
        fetched_slot = slot;
      }
    }

    slot = ( slot + 1 ) & ( table_size - 1 );
    probe_seq_iter++;
  }

  // Exceeded max probe sequence length, failed to put.
  // Rehash and try again.
  return false;
}

void Ember::SpatialHashMap::EraseCell( DirectX::XMINT3 const cell )
{
  if ( auto const slot = FetchSlot( cell ) )
  {
    m_TableData.Control[slot.value()] = kDeleted;
    m_Size--;
  }

  // Figure rehashing and shrinking strategy if necessary.
}

Ember::SpatialHashMap::SpatialHashMap(
    float const cell_size, uint32_t const initial_table_size, float const max_load_factor )
  : m_TableData{ initial_table_size }, m_CellSize{ cell_size }, m_Size{ 0 }, m_MaxLoadFactor{ max_load_factor }
{
  // ASSERT Power of Two
  ASSERT( IsPowerOfTwo( initial_table_size ) );
}

bool Ember::SpatialHashMap::Contains( DirectX::XMFLOAT3 const position ) const
{
  return FetchSlot( PositionToCell( position ) ).has_value();
}

uint32_t& Ember::SpatialHashMap::Get( DirectX::XMFLOAT3 const position )
{
  auto const cell = PositionToCell( position );
  if ( auto const slot = FetchSlot( cell ) )
  {
    return m_TableData.Index[slot.value()];
  }
  UNREACHABLE;
}

uint32_t Ember::SpatialHashMap::Get( DirectX::XMFLOAT3 const position ) const
{
  auto const cell = PositionToCell( position );
  if ( auto const slot = FetchSlot( cell ) )
  {
    return m_TableData.Index[slot.value()];
  }
  UNREACHABLE;
}

std::optional<uint32_t> Ember::SpatialHashMap::TryGet( DirectX::XMFLOAT3 const position ) const
{
  return FetchSlot( PositionToCell( position ) )
      .transform( [&]( uint32_t const slot ) { return m_TableData.Index[slot]; } );
}

void Ember::SpatialHashMap::Put( DirectX::XMFLOAT3 const position, uint32_t const index )
{
  /* if ( m_Size + 1 > m_TableData.GetSize() * m_MaxLoadFactor )
   {
     ENSURE( Rehash() );
   }*/

  auto const cell = PositionToCell( position );
  if ( PutCell( cell, index ) ) return;

  ENSURE( Rehash() );

  auto const res = PutCell( cell, index );
  ASSERT( res );
}

void Ember::SpatialHashMap::Erase( DirectX::XMFLOAT3 const position )
{
  return EraseCell( PositionToCell( position ) );
}

size_t Ember::SpatialHashMap::Size() const
{
  return m_Size;
}

size_t Ember::SpatialHashMap::GetSlotCount() const
{
  return m_TableData.GetSize();
}

std::span<uint32_t const> Ember::SpatialHashMap::GetControlStore()
{
  return m_TableData.Control;
}

std::span<uint32_t const> Ember::SpatialHashMap::GetIndirectionStore()
{
  return m_TableData.Index;
}

float Ember::SpatialHashMap::GetCellSize() const
{
  return m_CellSize;
}

uint32_t Ember::SpatialHashMap::operator[]( DirectX::XMFLOAT3 const position ) const
{
  return Get( position );
}

uint32_t& Ember::SpatialHashMap::operator[]( DirectX::XMFLOAT3 const position )
{
  return Get( position );
}
