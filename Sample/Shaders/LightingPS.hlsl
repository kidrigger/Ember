#include "Bindless.hlsli"
#include "Camera.hlsli"
#include "Colors.hlsli"
#include "DebugConfig.hlsli"
#include "Environment.hlsli"
#include "LightData.hlsli"
#include "PBR.hlsli"
#include "Quantization.hlsli"

cbuffer GBufferIn : register( b0 )
{
  ResID g_Position;
  ResID g_Albedo;
  ResID g_Normal;
  ResID g_ORM;
  ResID g_Emissive;
};

cbuffer FrameConstants : register( b1 )
{
  Camera      g_Camera;
  LightInfo   g_Lights;
  DebugConfig g_Debug;
}

cbuffer EnvBuf : register( b2 )
{
  Environment g_Env;
};

SamplerState           g_DefaultSampler : register( s0 );
SamplerState           g_ClampedSampler : register( s1 );
SamplerComparisonState g_ShadowSampler : register( s2 );
SamplerState           g_PointSampler : register( s3 );

float3                 CalcDirLightContrib( in BRDFCookTorranceGGX brdf, float4 ws_position, float3 view_dir )
{
  StructuredBuffer<DirLight> dir_lights  = ResourceDescriptorHeap[g_Lights.DirLights];

  float3                     dir_contrib = 0.0f;
  int                        light_idx   = 0;
  for ( ; light_idx < g_Lights.ShadowDirLightCount; light_idx++ )
  {
    dir_contrib += CalcShadowingLightContrib( dir_lights[light_idx], brdf, ws_position, view_dir, g_ShadowSampler );
  }

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
  for ( ; light_idx < g_Lights.ShadowPointLightCount; light_idx++ )
  {
    point_contrib += CalcShadowingLightContrib( point_lights[light_idx], brdf, ws_position, view_dir, g_ShadowSampler );
  }

  for ( ; light_idx < g_Lights.PointLightCount; light_idx++ )
  {
    point_contrib += CalcLightContrib( point_lights[light_idx], brdf, ws_position, view_dir );
  }

  return point_contrib;
}

float4 LightingPS( float2 tex_coord : TEXCOORD ) : SV_TARGET
{
  Texture2D<float4> position_tex = ResourceDescriptorHeap[g_Position];
  Texture2D<float4> albedo_tex   = ResourceDescriptorHeap[g_Albedo];
  Texture2D<float2> normal_tex   = ResourceDescriptorHeap[g_Normal];
  Texture2D<float4> orm_tex      = ResourceDescriptorHeap[g_ORM];
  Texture2D<float4> emissive_tex = ResourceDescriptorHeap[g_Emissive];

#ifndef STRIP_DEBUG_CONFIG
  switch ( g_Debug.VisualizationMode )
  {
    case kRender:
      break;
    case kMeshlet:
      return float4( albedo_tex.Sample( g_PointSampler, tex_coord ).rgb, 1.0f );
    case kWorldPosition:
      return float4( position_tex.Sample( g_PointSampler, tex_coord ).rgb, 1.0f );
    case kAlbedo:
      return float4( albedo_tex.Sample( g_PointSampler, tex_coord ).rgb, 1.0f );
    case kNormal:
      return float4( OctahedralDecode( normal_tex.Sample( g_PointSampler, tex_coord ) ), 1.0f );
    case kORM:
      return float4( orm_tex.Sample( g_PointSampler, tex_coord ).rgb, 1.0f );
    case kEmissive:
      return float4( emissive_tex.Sample( g_PointSampler, tex_coord ).rgb, 1.0f );
    case kLightingOnly:
      break;
  }
#endif

  // w channel of position texture is used for emissive strength
  float4 pos_emission = position_tex.Sample( g_PointSampler, tex_coord );

  float4 position     = float4( pos_emission.xyz, 1.0f );
  float3 albedo       = albedo_tex.Sample( g_PointSampler, tex_coord ).rgb;
  float3 normal       = OctahedralDecode( normal_tex.Sample( g_PointSampler, tex_coord ) );
  float3 orm          = orm_tex.Sample( g_PointSampler, tex_coord ).xyz;
  float3 emissive     = emissive_tex.Sample( g_PointSampler, tex_coord ).rgb * pos_emission.w;

#ifndef STRIP_DEBUG_CONFIG
  if ( g_Debug.VisualizationMode == kLightingOnly )
  {
    albedo.xyz = 0.5f;
  }
#endif

  BRDFCookTorranceGGX brdf;
  brdf.Albedo        = albedo;
  brdf.Normal        = normal;
  brdf.Metallic      = orm.z;
  brdf.Roughness     = orm.y;
  brdf.Occlusion     = orm.x;
  brdf.F0            = lerp( 0.04f, albedo.rgb, orm.z );

  float3 view_dir    = normalize( g_Camera.Position.xyz - position.xyz );

  float3 dir_contrib = CalcDirLightContrib( brdf, position, view_dir );

#ifdef STRIP_DEBUG_CONFIG
  float3 ambient_contrib = GetAmbientInfluence( brdf, view_dir, g_DefaultSampler, g_ClampedSampler );
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

  float3 total_contrib = emissive + dir_contrib + ambient_contrib;

  return float4( total_contrib, 1.0f );
}
