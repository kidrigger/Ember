#include "Bindless.hlsli"
#include "Camera.hlsli"
#include "Colors.hlsli"
#include "DebugConfig.hlsli"
#include "Environment.hlsli"
#include "PBR.hlsli"
#include "Quantization.hlsli"
#include "SpotLightCommon.hlsli"

cbuffer GBufferIn : register( b0 )
{
  ResID g_Position;
  ResID g_Albedo;
  ResID g_Normal;
  ResID g_ORM;
  ResID g_Emissive;
};

SamplerState           g_DefaultSampler : register( s0 );
SamplerState           g_ClampedSampler : register( s1 );
SamplerComparisonState g_ShadowSampler : register( s2 );
SamplerState           g_PointSampler : register( s3 );

float4                 SpotLightingPS( float4 screen_pos : SV_POSITION, uint light_idx : LIGHT_ID ) : SV_TARGET
{
  StructuredBuffer<SpotLight> spot_lights  = ResourceDescriptorHeap[g_Lights.SpotLights];

  Texture2D<float4>           position_tex = ResourceDescriptorHeap[g_Position];

  uint2                       tex_size;
  uint                        nlev;
  position_tex.GetDimensions( 0, tex_size.x, tex_size.y, nlev );
  float2            tex_coord    = screen_pos.xy / float2( tex_size );

  Texture2D<float4> albedo_tex   = ResourceDescriptorHeap[g_Albedo];
  Texture2D<float2> normal_tex   = ResourceDescriptorHeap[g_Normal];
  Texture2D<float4> orm_tex      = ResourceDescriptorHeap[g_ORM];
  Texture2D<float4> emissive_tex = ResourceDescriptorHeap[g_Emissive];

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
  brdf.Albedo     = albedo;
  brdf.Normal     = normal;
  brdf.Metallic   = orm.z;
  brdf.Roughness  = orm.y;
  brdf.Occlusion  = orm.x;
  brdf.F0         = lerp( 0.04f, albedo.rgb, orm.z );

  float3 view_dir = normalize( g_Camera.Position.xyz - position.xyz );

  float3 spot_contrib =
      light_idx < g_Lights.ShadowSpotLightCount
          ? CalcShadowingLightContrib( spot_lights[light_idx], brdf, position, view_dir, g_ShadowSampler )
          : CalcLightContrib( spot_lights[light_idx], brdf, position, view_dir );

  return float4( spot_contrib, 1.0f );
}
