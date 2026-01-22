#include "DebugConfig.hlsli"
#include "TrianglePSCommon.hlsli"

float4 TriangleMaskedPS( PSIn IN ) : SV_TARGET0
{
  StructuredBuffer<Material> materials = ResourceDescriptorHeap[g_DrawBatch.MaterialBuffer];

  Material                   mat       = materials[NonUniformResourceIndex( IN.Material )];

  float4                     albedo    = IN.Color * mat.GetAlbedo( IN.TexCoord, g_DefaultSampler );

  if ( albedo.a < mat.AlphaCutoff ) discard;

  float3 normal      = mat.GetNormal( IN.Normal, IN.Tangent, IN.Position.xyz, IN.TexCoord, g_DefaultSampler );
  float2 metal_rough = mat.GetMetalRough( IN.TexCoord, g_DefaultSampler );
  float3 emissive    = mat.GetEmissive( IN.TexCoord, g_DefaultSampler );

#ifndef STRIP_DEBUG_CONFIG
  switch ( g_Debug.VisualizationMode )
  {
    case kRender:
      break;
    case kMeshlet:
      return float4( IN.MeshletColor, 1.0f );
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
  brdf.Albedo          = albedo.xyz;
  brdf.Metallic        = metal_rough.r;
  brdf.Normal          = normal.xyz;
  brdf.Roughness       = metal_rough.g;
  brdf.F0              = lerp( 0.04f, albedo.rgb, metal_rough.x );
  brdf.Occlusion       = 1.0f;

  float3 view_dir      = normalize( g_Camera.Position.xyz - IN.Position.xyz );

  float3 point_contrib = CalcPointLightContrib( brdf, IN.Position, view_dir );
  float3 dir_contrib   = CalcDirLightContrib( brdf, IN.Position, view_dir );

#ifdef STRIP_DEBUG_CONFIG
  float3 ambient_contrib = GetAmbientInfluence( g_Env, brdf, view_dir, g_DefaultSampler, g_ClampedSampler );
#else
  float3 ambient_contrib = GetAmbientInfluence(
      g_Env,
      brdf,
      view_dir,
      g_DefaultSampler,
      g_ClampedSampler,
      !g_Debug.RemoveDiffuseContrib,
      !g_Debug.RemoveSpecularContrib );
#endif

  float3 total_contrib = emissive + point_contrib + dir_contrib + ambient_contrib;

  return float4( total_contrib, 1.0f );
}
