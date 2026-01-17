#include "DebugConfig.hlsli"
#include "PBR.hlsli"
#include "ReflectionProbe.hlsli"

float3 CalcDirLightContrib( in BRDFCookTorranceGGX brdf, float4 ws_position, float3 view_dir )
{
  StructuredBuffer<DirLight> dir_lights  = ResourceDescriptorHeap[g_DirLights];

  float3                     dir_contrib = 0.0f;
  int                        light_idx   = 0;
  //for ( ; light_idx < g_ShadowDirLightCount; light_idx++ )
  //{
  //  dir_contrib += CalcShadowingLightContrib( dir_lights[light_idx], brdf, ws_position, view_dir, g_ShadowSampler );
  //}

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
  //for ( ; light_idx < g_ShadowPointLightCount; light_idx++ )
  //{
  //  point_contrib += CalcShadowingLightContrib( point_lights[light_idx], brdf, ws_position, view_dir, g_ShadowSampler );
  //}

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
  //for ( ; light_idx < g_ShadowSpotLightCount; light_idx++ )
  //{
  //  spot_contrib += CalcShadowingLightContrib( spot_lights[light_idx], brdf, ws_position, view_dir, g_ShadowSampler );
  //}

  for ( ; light_idx < g_SpotLightCount; light_idx++ )
  {
    spot_contrib += CalcLightContrib( spot_lights[light_idx], brdf, ws_position, view_dir );
  }

  return spot_contrib;
}

float4 ReflectionProbePS( PSIn IN ) : SV_TARGET0
{
  StructuredBuffer<Material> materials = ResourceDescriptorHeap[g_Materials];

  //
  float3   view_dir    = normalize( g_ProbeInfo.xyz - IN.Position.xyz );

  Material mat         = materials[NonUniformResourceIndex( IN.Material )];

  float4   albedo      = mat.GetAlbedo( IN.TexCoord, g_DefaultSampler ) * IN.Color;
  float3   normal      = mat.GetNormal( IN.Normal, IN.Tangent, IN.Position.xyz, IN.TexCoord, g_DefaultSampler );
  float2   metal_rough = mat.GetMetalRough( IN.TexCoord, g_DefaultSampler );
  float3   emissive    = mat.GetEmissive( IN.TexCoord, g_DefaultSampler );

#ifndef STRIP_DEBUG_CONFIG
  ConstantBuffer<DebugConfig> config = ResourceDescriptorHeap[g_ConfigID];
  switch ( config.VisualizationMode )
  {
    case kRender:
      break;
    case kMeshlet:
      break;
    case kWorldPosition:
      return float4( IN.Position.xyz, 1.0f );
    case kAlbedo:
      return float4( albedo.xyz, 1.0f );
    case kNormal:
      return 0.5f * float4( normal, 1.0f ) + 0.5f;
    case kORM:
      return float4( float3( 1.0f, metal_rough.yx ), 1.0f );
    case kEmissive:
      return float4( emissive, 1.0f );
    case kLightingOnly:
      albedo.xyz = 0.5f;
      break;
  }
#endif

  BRDFCookTorranceGGX brdf;
  brdf.Albedo    = albedo.xyz;
  brdf.Metallic  = metal_rough.r;
  brdf.Normal    = normal.xyz;
  brdf.Roughness = metal_rough.g;
  brdf.F0        = lerp( 0.04f, albedo.rgb, metal_rough.x );
  brdf.Occlusion = 1.0f;

  float3 point_contrib;
  float3 spot_contrib;
  float3 dir_contrib;
  float3 ambient_contrib;

  {
    point_contrib = CalcPointLightContrib( brdf, IN.Position, view_dir );
    spot_contrib  = CalcSpotLightContrib( brdf, IN.Position, view_dir );
    dir_contrib   = CalcDirLightContrib( brdf, IN.Position, view_dir );

#ifdef STRIP_DEBUG_CONFIG
    ambient_contrib = GetAmbientInfluence( g_Env, brdf, view_dir, g_DefaultSampler, g_ClampedSampler );
#else
    ambient_contrib = GetAmbientInfluence(
        g_Env,
        brdf,
        view_dir,
        g_DefaultSampler,
        g_ClampedSampler,
        !config.RemoveDiffuseContrib,
        !config.RemoveSpecularContrib );
#endif
  }

  float3 total_contrib = emissive + point_contrib + dir_contrib + spot_contrib + ambient_contrib;

  return float4( total_contrib, 1.0f );
}
