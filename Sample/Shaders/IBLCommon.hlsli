#ifndef IBL_COMMON_HLSLI_
#define IBL_COMMON_HLSLI_

#include "Utility.hlsli"

float RadicalInverseVdC( uint bits )
{
  bits = ( bits << 16u ) | ( bits >> 16u );
  bits = ( ( bits & 0x55555555u ) << 1u ) | ( ( bits & 0xAAAAAAAAu ) >> 1u );
  bits = ( ( bits & 0x33333333u ) << 2u ) | ( ( bits & 0xCCCCCCCCu ) >> 2u );
  bits = ( ( bits & 0x0F0F0F0Fu ) << 4u ) | ( ( bits & 0xF0F0F0F0u ) >> 4u );
  bits = ( ( bits & 0x00FF00FFu ) << 8u ) | ( ( bits & 0xFF00FF00u ) >> 8u );
  return float( bits ) * 2.3283064365386963e-10; // / 0x100000000
}

float2 Hammersley( uint sample_index, uint sample_count )
{
  return float2( float( sample_index ) / float( sample_count ), RadicalInverseVdC( sample_index ) );
}

float3 ImportanceSampleGGX( float2 xi, float3 normal, float roughness )
{
  float a         = roughness * roughness;

  float phi       = 2.0f * kPi * xi.x;
  float cos_theta = saturate( sqrt( ( 1.0f - xi.y ) / ( 1.0f + ( a * a - 1.0f ) * xi.y ) ) );
  float sin_theta = sqrt( 1.0f - cos_theta * cos_theta );

  // from spherical coordinates to cartesian coordinates
  float3 h;
  h.x = cos( phi ) * sin_theta;
  h.y = sin( phi ) * sin_theta;
  h.z = cos_theta;

  // from tangent-space vector to world-space sample vector
  float3 up         = abs( normal.z ) < 0.999f ? float3( 0.0f, 0.0f, 1.0f ) : float3( 1.0f, 0.0f, 0.0f );
  float3 tangent    = normalize( cross( up, normal ) );
  float3 bitangent  = cross( normal, tangent );

  float3 sample_vec = tangent * h.x + bitangent * h.y + normal * h.z;
  return normalize( sample_vec );
}

#endif
