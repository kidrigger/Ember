#ifndef TRIANGLE_PS_COMMON_HLSLI_
#define TRIANGLE_PS_COMMON_HLSLI_

#include "Math.hlsli"
#include "PBR.hlsli"
#include "Triangle.hlsli"

float3 CalcDirLightContrib( in BRDFCookTorranceGGX brdf, float4 ws_position, float3 view_dir )
{
  StructuredBuffer<DirLight> dir_lights  = ResourceDescriptorHeap[g_DirLights];

  float3                     dir_contrib = 0.0f;
  int                        light_idx   = 0;
  for ( ; light_idx < g_ShadowDirLightCount; light_idx++ )
  {
    dir_contrib += CalcShadowingLightContrib( dir_lights[light_idx], brdf, ws_position, view_dir, g_ShadowSampler );
  }

  for ( ; light_idx < g_DirLightCount; light_idx++ )
  {
    dir_contrib += CalcLightContrib( dir_lights[light_idx], brdf, ws_position, view_dir );
  }

  return dir_contrib;
}

float3 CalcPointLightContrib( in BRDFCookTorranceGGX brdf, float4 ws_position, float3 view_dir )
{
  StructuredBuffer<PointLight> point_lights  = ResourceDescriptorHeap[g_PointLights];

  float3                       point_contrib = 0.0f;
  int                          light_idx     = 0;
  for ( ; light_idx < g_ShadowPointLightCount; light_idx++ )
  {
    point_contrib += CalcShadowingLightContrib( point_lights[light_idx], brdf, ws_position, view_dir, g_ShadowSampler );
  }

  for ( ; light_idx < g_PointLightCount; light_idx++ )
  {
    point_contrib += CalcLightContrib( point_lights[light_idx], brdf, ws_position, view_dir );
  }

  return point_contrib;
}

#endif
