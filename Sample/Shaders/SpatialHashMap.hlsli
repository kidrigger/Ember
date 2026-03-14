#ifndef SPATIAL_HASH_MAP_HLSLI_
#define SPATIAL_HASH_MAP_HLSLI_

#define SHM_SEARCH_COUNT 10
#define SHM_USED_BIT 0x80000000
#define SHM_DELETED 0x7FFFFFFF

uint Murmur3Hash( uint k )
{
  k ^= k >> 16;
  k *= 0x85ebca6b;
  k ^= k >> 13;
  k *= 0xc2b2ae35;
  k ^= k >> 16;
  return k;
}

uint XxHash32( uint p )
{
  const uint PRIME32_2 = 2246822519U, PRIME32_3 = 3266489917U;
  const uint PRIME32_4 = 668265263U, PRIME32_5 = 374761393U;
  uint       h32 = p + PRIME32_5;
  h32            = PRIME32_4 * ( ( h32 << 17 ) | ( h32 >> ( 32 - 17 ) ) );
  h32            = PRIME32_2 * ( h32 ^ ( h32 >> 15 ) );
  h32            = PRIME32_3 * ( h32 ^ ( h32 >> 13 ) );
  return h32 ^ ( h32 >> 16 );
}

uint SpatialHashMap_HashSlot( in int3 cell, in float cell_size )
{
  uint cell_size_bits = asuint( cell_size );
  return XxHash32( cell_size_bits + XxHash32( cell.x + XxHash32( cell.y + XxHash32( cell.z ) ) ) );
}

uint SpatialHashMap_HashControl( in int3 cell, in float cell_size )
{
  uint cell_size_bits = asuint( cell_size );
  return SHM_USED_BIT |
         Murmur3Hash( cell_size_bits + Murmur3Hash( cell.x + Murmur3Hash( cell.y + Murmur3Hash( cell.z ) ) ) );
}

uint SpatialHashMap_Fetch( in ByteAddressBuffer buffer, in uint table_size, float3 position, float cellsize )
{
  uint3 cell       = int3( floor( position / cellsize ) );
  uint  hash_value = SpatialHashMap_HashSlot( cell, cellsize );
  uint  hash_check = SpatialHashMap_HashControl( cell, cellsize );

  uint  slot       = hash_value & ( table_size - 1 );
  for ( uint i = 0; i < SHM_SEARCH_COUNT; i++ )
  {
    uint check = buffer.Load( slot << 2 );

    // If unused, we end probing.
    // Don't have the key in the table.
    if ( ( check & SHM_USED_BIT ) == 0 && ( check != SHM_DELETED ) )
    {
      return 0xFFFFFFFF;
    }

    if ( check == hash_check )
    {
      return buffer.Load( ( table_size + slot ) << 2 ); // value is stored after the control slots
    }
    slot = ( slot + 1 ) & ( table_size - 1 );
  }
  return 0xFFFFFFFF;
}

#undef SHM_SEARCH_COUNT
#undef SHM_USED_BIT
#undef SHM_DELETED

#endif // SPATIAL_HASH_MAP_HLSLI_
