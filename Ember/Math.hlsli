#ifndef MATH_HLSLI_
#define MATH_HLSLI_

// Sphere repr (cx, cy, cz, r)
bool PointInsideSphere( float3 pnt, float4 sphere )
{
  return distance( pnt, sphere.xyz ) < sphere.w;
}

#endif
