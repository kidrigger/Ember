#ifndef MATH_HLSLI_
#define MATH_HLSLI_

#include "Constants.hlsli"

float3 MulQuatVec( float4 quat, float3 vec )
{
  // Based on Euler-Rodriques' formula
  // v + 2 * cross(r, (s * v + cross(r, v))) / m
  return vec + 2.0f * cross( quat.xyz, ( quat.w * vec + cross( quat.xyz, vec ) ) ) / dot( quat, quat );
}

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

// Polar form intersection
// ray_origin_r: distance from the ray origin to the center of the sphere (r = ||x||)
// ray_direction_mu: cosine of the angle between the ray direction and the vertical axis dot(norm(x), v)
bool DistanceToSpherePolar( out float distance, float sphere_radius, float ray_origin_r, float ray_direction_mu )
{
  distance   = kInf;

  float b    = ray_origin_r * ray_direction_mu;
  float c    = ray_origin_r * ray_origin_r - sphere_radius * sphere_radius;
  float disc = b * b - c;

  if ( disc < 0.0f ) return false;

  float delta        = sqrt( disc );
  float distance_val = -b - delta;

  distance_val       = distance_val + step( distance_val, 0.0f ) * 2 * delta;

  if ( distance_val < 0.0f ) return false;

  distance = distance_val;

  return true;
}

// Polar form intersection, assuming the ray origin is inside the sphere
// Undefined behavior if the ray origin is outside the sphere
//
// ray_origin_r: distance from the ray origin to the center of the sphere (r = ||x||)
// ray_direction_mu: cosine of the angle between the ray direction and the vertical axis dot(norm(x), v)
void DistanceToSphereFromInsidePolar(
    out float distance, float sphere_radius, float ray_origin_r, float ray_direction_mu )
{
  float b     = ray_origin_r * ray_direction_mu;
  float c     = ray_origin_r * ray_origin_r - sphere_radius * sphere_radius;
  float delta = sqrt( b * b - c );
  distance    = -b + delta;
}

#endif
