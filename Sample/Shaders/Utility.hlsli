#ifndef UTILITY_HLSLI_
#define UTILITY_HLSLI_

#include "Constants.hlsli"

#define OUTPUT_TOPOLOGY( x ) [outputtopology( x )]
#define NUM_THREADS( x, y, z ) [numthreads( x, y, z )]

/*
 The Goal is simply to convert from a (0,0,0) to (CubeSide, CubeSide, FaceCount) Invocation ID space to a
 (-1,-1,-1) to (1, 1, 1) space.

| Axis | Layer | Up |
|:----:|:-----:|:--:|
|  +x  |   0   | +y |
|  -x  |   1   | +y |
|  +y  |   2   | -z |
|  -y  |   3   | +z |
|  -z  |   4   | +y |
|  +z  |   5   | +y |
*/
float3 GetCubeDir( float2 face_xy, uint layer, float texel_size )
{
  float2 face_uv = face_xy * texel_size;  // (0, SideLength) -> (0, 1)
  face_uv        = 2.0f * face_uv - 1.0f; // (0, 1) -> (-1, 1)

  switch ( layer )
  {
    case 0:
      return normalize( float3( 1.0f, -face_uv.y, -face_uv.x ) );  // Face +X; x = 1, y = -v, z = -u
    case 1:
      return normalize( float3( -1.0f, -face_uv.y, face_uv.x ) );  // Face -X; x = -1, y = -v, z = u
    case 2:
      return normalize( float3( face_uv.x, 1.0f, face_uv.y ) );    // Face +Y; x = u, y = 1, z = v
    case 3:
      return normalize( float3( face_uv.x, -1.0f, -face_uv.y ) );  // Face -Y; x=u, y=-1, z=-v
    case 4:
      return normalize( float3( face_uv.x, -face_uv.y, 1.0f ) );   // Face +Z; x=u,y=-v, z=1
    case 5:
      return normalize( float3( -face_uv.x, -face_uv.y, -1.0f ) ); // Face -Z; x=u,y=-v, z=-1
    default:
      // Never reach here.
      return 0.0f;
  }
}

uint3 LoadBytes3( in ByteAddressBuffer buffer, uint byte_offset )
{
  uint  buf_offset = ( byte_offset & ~3 );
  uint  sub_offset = ( byte_offset & 3 );
  uint2 value      = buffer.Load2( buf_offset );

  return uint3(
      value[sub_offset >> 2] >> ( ( sub_offset & 0x3 ) * 8 ) & 0xFF,
      value[( sub_offset + 1 ) >> 2] >> ( ( ( sub_offset + 1 ) & 0x3 ) * 8 ) & 0xFF,
      value[( sub_offset + 2 ) >> 2] >> ( ( ( sub_offset + 2 ) & 0x3 ) * 8 ) & 0xFF );
}

#endif
