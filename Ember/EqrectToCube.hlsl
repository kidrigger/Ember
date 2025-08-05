#include "Bindless.hlsli"
#include "Utility.hlsli"

cbuffer PCB : register( b0, space0 )
{
  RID  g_EqrectHandle;
  RID  g_OutputCubemapHandle;
  uint g_CubeSide;
}

SamplerState g_EqrectSampler : register( s0, space0 );

float2       SampleSphericalMap( float3 v )
{
  const float2 inv_tan  = float2( 0.1591f, 0.3183f );                 // (1/2PI, 1/PI)
  float2       uv       = float2( atan2( v.x, -v.z ), asin( -v.y ) ); // (-PI, -PI/2) to (PI, PI/2)
  uv                   *= inv_tan;                                    // (-1/2, -1/2) to (1/2, 1/2)
  uv                   += 0.5f;                                       // (0, 0) to (1, 1)
  return uv;
}

// This function requires
NUMTHREADS( 16, 16, 1 )
void EqrectToCube( uint3 global_invocation_id : SV_DispatchThreadID )
{
  float3                   local_dir = GetCubeDir( global_invocation_id.xy, global_invocation_id.z, 1.0f / g_CubeSide );

  Texture2D                eqrect_tex = ResourceDescriptorHeap[g_EqrectHandle];
  RWTexture2DArray<float3> output_tex = ResourceDescriptorHeap[g_OutputCubemapHandle];

  float2                   uv         = SampleSphericalMap( local_dir );
  output_tex[global_invocation_id]    = eqrect_tex.SampleLevel( g_EqrectSampler, uv, 0 ).rgb;
}
