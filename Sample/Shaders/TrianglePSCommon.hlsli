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

float3 CalcSpotLightContrib( in BRDFCookTorranceGGX brdf, float4 ws_position, float3 view_dir )
{
  StructuredBuffer<SpotLight> spot_lights  = ResourceDescriptorHeap[g_SpotLights];

  float3                      spot_contrib = 0.0f;
  int                         light_idx    = 0;
  for ( ; light_idx < g_ShadowSpotLightCount; light_idx++ )
  {
    spot_contrib += CalcShadowingLightContrib( spot_lights[light_idx], brdf, ws_position, view_dir, g_ShadowSampler );
  }

  for ( ; light_idx < g_SpotLightCount; light_idx++ )
  {
    spot_contrib += CalcLightContrib( spot_lights[light_idx], brdf, ws_position, view_dir );
  }

  return spot_contrib;
}

template <typename TRayQuery>
float3 CalcDirLightContrib( in BRDFCookTorranceGGX brdf, float4 ws_position, float3 view_dir, in TRayQuery rq )
{
  StructuredBuffer<DirLight>      dir_lights  = ResourceDescriptorHeap[g_DirLights];
  RaytracingAccelerationStructure tlas        = ResourceDescriptorHeap[g_DrawBatch.TopLevelAS];

  float3                          dir_contrib = 0.0f;
  int                             light_idx   = 0;
  for ( ; light_idx < g_ShadowDirLightCount; light_idx++ )
  {
    dir_contrib += CalcShadowingLightContrib( dir_lights[light_idx], brdf, ws_position, view_dir, rq, tlas );
  }

  for ( ; light_idx < g_DirLightCount; light_idx++ )
  {
    dir_contrib += CalcLightContrib( dir_lights[light_idx], brdf, ws_position, view_dir );
  }

  return dir_contrib;
}

template <typename TRayQuery>
float3 CalcPointLightContrib( in BRDFCookTorranceGGX brdf, float4 ws_position, float3 view_dir, in TRayQuery rq )
{
  StructuredBuffer<PointLight>    point_lights  = ResourceDescriptorHeap[g_PointLights];
  RaytracingAccelerationStructure tlas          = ResourceDescriptorHeap[g_DrawBatch.TopLevelAS];

  float3                          point_contrib = 0.0f;
  int                             light_idx     = 0;
  for ( ; light_idx < g_ShadowPointLightCount; light_idx++ )
  {
    point_contrib += CalcShadowingLightContrib( point_lights[light_idx], brdf, ws_position, view_dir, rq, tlas );
  }

  for ( ; light_idx < g_SpotLightCount; light_idx++ )
  {
    point_contrib = CalcLightContrib( point_lights[light_idx], brdf, ws_position, view_dir );
  }

  return point_contrib;
}

template <typename TRayQuery>
float3 CalcSpotLightContrib( in BRDFCookTorranceGGX brdf, float4 ws_position, float3 view_dir, in TRayQuery rq )
{
  StructuredBuffer<SpotLight>     spot_lights  = ResourceDescriptorHeap[g_SpotLights];
  RaytracingAccelerationStructure tlas         = ResourceDescriptorHeap[g_DrawBatch.TopLevelAS];

  float3                          spot_contrib = 0.0f;
  int                             light_idx    = 0;
  for ( ; light_idx < g_ShadowSpotLightCount; light_idx++ )
  {
    spot_contrib += CalcShadowingLightContrib( spot_lights[light_idx], brdf, ws_position, view_dir, rq, tlas );
  }

  for ( ; light_idx < g_SpotLightCount; light_idx++ )
  {
    spot_contrib += CalcLightContrib( spot_lights[light_idx], brdf, ws_position, view_dir );
  }

  return spot_contrib;
}

#endif
