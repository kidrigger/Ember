#pragma once

#include <Util/DirectXHeaders.hpp>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "Environment.hpp"

namespace Ember
{

/*
 * SpatialHashMap maps spaces (divided into cells) to indexes to a separate dense array. (User managed)
 * The base hash map is expected to probe < kMaxProbeSeqLen times.
 * So might have lots of empty spaces.
 * The value might be large so it's separate.
 */
class SpatialHashMap
{
  static constexpr uint32_t kUsedBit        = 1u << 31;
  static constexpr uint32_t kDeleted        = ~kUsedBit;
  static constexpr uint32_t kMaxProbeSeqLen = 10;
  static_assert( kMaxProbeSeqLen < 256, "kMaxProbeSeqLen fit in uint8_t" );

  struct TableData
  {
    std::vector<uint32_t>        Control;
    std::vector<DirectX::XMINT3> Cells;
    std::vector<uint32_t>        Index;       // GPU index, effectively the value of the hash table.
    std::vector<uint8_t>         ProbeSeqLen; // Never exceed 256. Doesn't make sense.

    explicit TableData( size_t table_size );
    [[nodiscard]] size_t GetSize() const noexcept;
  };

  TableData                             m_TableData;
  float                                 m_CellSize;
  size_t                                m_Size;
  float                                 m_MaxLoadFactor;

  static uint32_t                       Murmur3Hash( uint32_t k );
  static uint32_t                       XxHash32( uint32_t p );
  [[nodiscard]] uint32_t                HashSlot( DirectX::XMINT3 cell ) const;
  [[nodiscard]] uint32_t                HashControl( DirectX::XMINT3 cell ) const;

  static bool                           IsUsed( uint32_t value );
  static bool                           IsDeleted( uint32_t value );

  bool                                  Rehash();

  [[nodiscard]] DirectX::XMINT3         PositionToCell( DirectX::XMFLOAT3 position ) const;
  [[nodiscard]] std::optional<uint32_t> FetchSlot( DirectX::XMINT3 cell ) const;
  [[nodiscard]] bool                    PutCell( DirectX::XMINT3 cell, uint32_t index );
  void                                  EraseCell( DirectX::XMINT3 cell );

public:
  SpatialHashMap( float cell_size, uint32_t initial_table_size, float max_load_factor = 0.9f );

  [[nodiscard]] bool                      Contains( DirectX::XMFLOAT3 position ) const;
  [[nodiscard]] uint32_t&                 Get( DirectX::XMFLOAT3 position );
  [[nodiscard]] uint32_t                  Get( DirectX::XMFLOAT3 position ) const;
  [[nodiscard]] std::optional<uint32_t>   TryGet( DirectX::XMFLOAT3 position ) const;
  void                                    Put( DirectX::XMFLOAT3 position, uint32_t index );
  void                                    Erase( DirectX::XMFLOAT3 position );

  [[nodiscard]] size_t                    Size() const;
  [[nodiscard]] size_t                    GetSlotCount() const;
  [[nodiscard]] std::span<uint32_t const> GetControlStore();     // The control array of the map.
  [[nodiscard]] std::span<uint32_t const> GetIndirectionStore(); // The index array of the map.
  [[nodiscard]] float                     GetCellSize() const;

  uint32_t                                operator[]( DirectX::XMFLOAT3 const position ) const;
  uint32_t&                               operator[]( DirectX::XMFLOAT3 const position );
};
} // namespace Ember
