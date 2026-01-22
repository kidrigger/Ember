#include "DebugConfig.hlsli"
#include "PBR.hlsli"
#include "ReflectionProbe.hlsli"

float3 CalcDirLightContrib( in BRDFCookTorranceGGX brdf, float4 ws_position, float3 view_dir )
{
  StructuredBuffer<DirLight> dir_lights  = ResourceDescriptorHeap[g_Lights.DirLights];

  float3                     dir_contrib = 0.0f;
  int                        light_idx   = 0;
  // for ( ; light_idx < g_Lights.ShadowDirLightCount; light_idx++ )
  //{
  //   dir_contrib += CalcShadowingLightContrib( dir_lights[light_idx], brdf, ws_position, view_dir, g_ShadowSampler );
  // }

  for ( ; light_idx < g_Lights.DirLightCount; light_idx++ )
  {
    dir_contrib += CalcLightContrib( dir_lights[light_idx], brdf, ws_position, view_dir );
  }

  return dir_contrib;
}

float3 CalcPointLightContrib( in BRDFCookTorranceGGX brdf, float4 ws_position, float3 view_dir )
{
  StructuredBuffer<PointLight> point_lights  = ResourceDescriptorHeap[g_Lights.PointLights];

  float3                       point_contrib = 0.0f;
  int                          light_idx     = 0;
  // for ( ; light_idx < g_Lights.ShadowPointLightCount; light_idx++ )
  //{
  //   point_contrib += CalcShadowingLightContrib( point_lights[light_idx], brdf, ws_position, view_dir, g_ShadowSampler
  //   );
  // }

  for ( ; light_idx < g_Lights.PointLightCount; light_idx++ )
  {
    point_contrib += CalcLightContrib( point_lights[light_idx], brdf, ws_position, view_dir );
  }

  return point_contrib;
}

float3 CalcSpotLightContrib( in BRDFCookTorranceGGX brdf, float4 ws_position, float3 view_dir )
{
  StructuredBuffer<SpotLight> spot_lights  = ResourceDescriptorHeap[g_Lights.SpotLights];

  float3                      spot_contrib = 0.0f;
  int                         light_idx    = 0;
  // for ( ; light_idx < g_Lights.ShadowSpotLightCount; light_idx++ )
  //{
  //   spot_contrib += CalcShadowingLightContrib( spot_lights[light_idx], brdf, ws_position, view_dir, g_ShadowSampler
  //   );
  // }

  for ( ; light_idx < g_Lights.SpotLightCount; light_idx++ )
  {
    spot_contrib += CalcLightContrib( spot_lights[light_idx], brdf, ws_position, view_dir );
  }

  return spot_contrib;
}

float4 ReflectionProbePS( PSIn IN ) : SV_TARGET0
{
  StructuredBuffer<Material> materials = ResourceDescriptorHeap[g_DrawBatch.MaterialBuffer];

  //
  float3              view_dir = normalize( g_ProbeInfo.xyz - IN.Position.xyz );

  Material            mat      = materials[NonUniformResourceIndex( IN.Material )];

  float4              albedo   = mat.GetAlbedo( IN.TexCoord, g_DefaultSampler ) * IN.Color;
  float3              normal   = mat.GetNormal( IN.Normal, IN.Tangent, IN.Position.xyz, IN.TexCoord, g_DefaultSampler );
  float2              metal_rough = mat.GetMetalRough( IN.TexCoord, g_DefaultSampler );
  float3              emissive    = mat.GetEmissive( IN.TexCoord, g_DefaultSampler );

  BRDFCookTorranceGGX brdf;
  brdf.Albedo            = albedo.xyz;
  brdf.Metallic          = metal_rough.r;
  brdf.Normal            = normal.xyz;
  brdf.Roughness         = metal_rough.g;
  brdf.F0                = lerp( 0.04f, albedo.rgb, metal_rough.x );
  brdf.Occlusion         = 1.0f;

  float3 point_contrib   = CalcPointLightContrib( brdf, IN.Position, view_dir );
  float3 spot_contrib    = CalcSpotLightContrib( brdf, IN.Position, view_dir );
  float3 dir_contrib     = CalcDirLightContrib( brdf, IN.Position, view_dir );

  float3 ambient_contrib = GetAmbientInfluence( g_Env, brdf, view_dir, g_DefaultSampler, g_ClampedSampler, true, true );

  float3 total_contrib   = point_contrib + dir_contrib + spot_contrib + ambient_contrib;

  return float4( total_contrib, 1.0f );
}
