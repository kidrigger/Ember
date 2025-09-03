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
  float3 center = mul( transformation, float4( sphere.xyz, 1.0f ) ).xyz;

  float  sx     = transformation[0][0];
  float  sy     = transformation[1][1];
  float  sz     = transformation[2][2];
  float  scale  = sqrt( max( sx * sx, max( sy * sy, sz * sz ) ) );

  return float4( center, scale * sphere.w );
}

float4 TransformCone( in float4x4 inv_transformation, float4 cone )
{
  // Switching order-of-operations to avoid transposing.
  return float4( normalize( mul( float4( cone.xyz, 0.0f ), inv_transformation ).xyz ), cone.w );
}

#endif
