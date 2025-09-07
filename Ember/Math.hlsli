#ifndef MATH_HLSLI_
#define MATH_HLSLI_

// Sphere repr (cx, cy, cz, r)
bool PointInsideSphere( float3 pnt, float4 sphere )
{
  return distance( pnt, sphere.xyz ) < sphere.w;
}

float PlaneSignedDistance( float4 plane, float3 position )
{
  return dot( plane.xyz, position ) - plane.w;
}

float4 TransformBoundingSphere( in float4x4 transformation, float4 sphere )
{
  float3   center = mul( transformation, float4( sphere.xyz, 1.0f ) ).xyz;

  float4x4 ttx    = transpose( transformation );
  float    sx     = dot( ttx[0], ttx[0] );
  float    sy     = dot( ttx[1], ttx[1] );
  float    sz     = dot( ttx[2], ttx[2] );
  float    scale  = sqrt( max( sx, max( sy, sz ) ) );

  return float4( center, scale * sphere.w );
}

float4 TransformCone( in float4x4 inv_transformation, float4 cone )
{
  // Switching order-of-operations to avoid transposing.
  return float4( normalize( mul( float4( cone.xyz, 0.0f ), inv_transformation ).xyz ), cone.w );
}

// Lowerbias 32-bit by u/skeeto
// https://www.reddit.com/r/RNG/comments/jqnq20/the_wang_and_jenkins_integer_hash_functions_just/
uint SimpleHash( uint x )
{
  x ^= x >> 16;
  x *= 0xa812d533;
  x ^= x >> 15;
  x *= 0xb278e4ad;
  x ^= x >> 17;
  return x;
}

#endif
