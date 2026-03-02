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


float ManhattenDistance( float3 x, float3 y )
{
  return abs( x.x - y.x ) + abs( x.y - y.y ) + abs( x.z - y.z );
}

float3 DepthToViewPos( in float4x4 inv_proj, in Texture2D<float> depth_tex, float2 uv, in SamplerState samp )
{
  float  depth          = depth_tex.SampleLevel( samp, uv, 0 );
  float4 position_view  = mul( inv_proj, float4( uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, depth, 1.0f ) );
  position_view.xyz    /= position_view.w;
  return position_view.xyz;
}

float3 DepthToNormal( in float4x4 inv_proj, in Texture2D<float> depth_tex, float2 uv, in SamplerState samp )
{
  float width, height;
  depth_tex.GetDimensions( width, height );
  float2 xoffset = float2( 1.0f / width, 0.0f );
  float2 yoffset = float2( 0.0f, 1.0f / height );

  float3 p0      = DepthToViewPos( inv_proj, depth_tex, uv, samp );
  float3 pr      = DepthToViewPos( inv_proj, depth_tex, uv + xoffset, samp );
  float3 pu      = DepthToViewPos( inv_proj, depth_tex, uv + yoffset, samp );
  float3 pl      = DepthToViewPos( inv_proj, depth_tex, uv - xoffset, samp );
  float3 pd      = DepthToViewPos( inv_proj, depth_tex, uv - yoffset, samp );

  // Right sets bit 0, Up sets bit 1. Pick the two depths closest to center.
  int choice = ( ManhattenDistance( pr, p0 ) < ManhattenDistance( pl, p0 ) ? 1 : 0 ) +
               ( ManhattenDistance( pu, p0 ) < ManhattenDistance( pd, p0 ) ? 2 : 0 );

  float3 normal;
  if ( choice == 0 )      // left, down
    normal = cross( pl - p0, pd - p0 );
  else if ( choice == 1 ) // right, down
    normal = cross( pd - p0, pr - p0 );
  else if ( choice == 2 ) // left, up
    normal = cross( pu - p0, pl - p0 );
  else                    // choice == 3 // right, up
    normal = cross( pr - p0, pu - p0 );

  return -normalize( normal );
}

#endif
