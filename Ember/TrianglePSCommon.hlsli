#ifndef TRIANGLE_PS_COMMON_HLSLI_
#define TRIANGLE_PS_COMMON_HLSLI_

#include "Math.hlsli"
#include "PBR.hlsli"
#include "Triangle.hlsli"

float3 SampleIrradiance( float3 direction )
{
  if ( IsValidHandle( g_Env.DiffuseIrradiance ) )
  {
    TextureCube diff_irr = ResourceDescriptorHeap[g_Env.DiffuseIrradiance];
    return diff_irr.Sample( g_DefaultSampler, direction ).rgb;
  }
  return 0.04f;
}

float3 SamplePrefiltered( float3 direction, float roughness )
{
  const static float kMaxMipLevel = 5.0f;
  if ( IsValidHandle( g_Env.PrefilterMap ) )
  {
    float       mip       = kMaxMipLevel * roughness;
    TextureCube prefilter = ResourceDescriptorHeap[g_Env.PrefilterMap];
    return prefilter.SampleLevel( g_DefaultSampler, direction, mip ).rgb;
  }
  return 0.0f;
}

float2 SampleBrdfLut( float n_dot_v, float roughness )
{
  if ( IsValidHandle( g_Env.BrdfLUT ) )
  {
    Texture2D<float2> brdf_lut = ResourceDescriptorHeap[g_Env.BrdfLUT];
    return brdf_lut.Sample( g_ClampedSampler, float2( n_dot_v, roughness ) );
  }
  return 0.0f;
}

float3 GetAmbientInfluence( BRDFCookTorranceGGX brdf, float3 view_dir )
{
  float cosine_factor =
      max( dot( brdf.Normal, view_dir ), 0.0f ); // Normal instead of Halfway since there's no halfway in ambient.

  float3 f_0                = 0.04f;
  f_0                       = lerp( f_0, brdf.Albedo, brdf.Metallic );
  float3 specular_part      = FresnelSchlickRoughness( cosine_factor, f_0, brdf.Roughness );
  float3 diffuse_part       = 1.0f - specular_part;

  diffuse_part             *= 1.0f - brdf.Metallic; // Metals don't have diffuse/refractions.

  float3 reflection_dir     = reflect( -view_dir, brdf.Normal );

  float  n_dot_v            = max( dot( brdf.Normal, view_dir ), 0.0f );
  float3 prefiltered_color  = SamplePrefiltered( reflection_dir, brdf.Roughness ).rgb;
  float2 env_brdf           = SampleBrdfLut( n_dot_v, brdf.Roughness );
  float3 specular           = prefiltered_color * ( specular_part * env_brdf.x + env_brdf.y );

  float3 diffuse            = brdf.Albedo * SampleIrradiance( brdf.Normal );

  return ( diffuse_part * diffuse + specular ) * brdf.Occlusion;
}

float3 GetAmbientInfluence( BRDFCookTorranceGGX brdf, float3 view_dir, bool use_diffuse, bool use_spec )
{
  float cosine_factor =
      max( dot( brdf.Normal, view_dir ), 0.0f ); // Normal instead of Halfway since there's no halfway in ambient.

  float3 f_0             = 0.04f;
  f_0                    = lerp( f_0, brdf.Albedo, brdf.Metallic );
  float3 specular_part   = FresnelSchlickRoughness( cosine_factor, f_0, brdf.Roughness );
  float3 diffuse_part    = 1.0f - specular_part;

  diffuse_part          *= 1.0f - brdf.Metallic; // Metals don't have diffuse/refractions.

  float3 reflection_dir  = reflect( -view_dir, brdf.Normal );

  float3 specular        = 0.0f.xxx;
  float3 diffuse         = 0.0f.xxx;
  if ( use_spec )
  {
    float  n_dot_v           = max( dot( brdf.Normal, view_dir ), 0.0f );
    float3 prefiltered_color = SamplePrefiltered( reflection_dir, brdf.Roughness ).rgb;
    float2 env_brdf          = SampleBrdfLut( n_dot_v, brdf.Roughness );
    specular                 = prefiltered_color * ( specular_part * env_brdf.x + env_brdf.y );
  }
  if ( use_diffuse )
  {
    diffuse = brdf.Albedo * SampleIrradiance( brdf.Normal );
  }

  return ( diffuse_part * diffuse + specular ) * brdf.Occlusion;
}

float3 CalcPointLightContrib( float4 ws_position, float3 view_dir, in BRDFCookTorranceGGX brdf )
{
  if ( !IsValidHandle( g_PointLights ) ) return 0.0f;

  StructuredBuffer<PointLight> point_lights  = ResourceDescriptorHeap[g_PointLights];

  float3                       point_contrib = 0.0f;
  int                          light_idx     = 0;
  for ( ; light_idx < g_ShadowPointLightCount; light_idx++ )
  {
    float3 light_dir  = float3( point_lights[light_idx].Position ) - ws_position.xyz;
    float  light_dist = length( light_dir );

    if ( light_dist > point_lights[light_idx].Range ) continue;

    // Shadow test
    TextureCube<float> shadow_map = ResourceDescriptorHeap[point_lights[light_idx].ShadowIdx];
    float              shadowing =
        shadow_map.SampleCmpLevelZero( g_ShadowSampler, -light_dir, light_dist / point_lights[light_idx].Range );

    float  attenuation = 1.0f / ( light_dist * light_dist ); // TODO: Controlled Attenuation
    float3 radiance =
        point_lights[light_idx].Intensity * UnpackColor32( point_lights[light_idx].Color ).rgb * attenuation;

    light_dir     /= light_dist; // Normalization
    point_contrib += shadowing * brdf.Evaluate( radiance, view_dir, light_dir );
  }

  for ( ; light_idx < g_PointLightCount; light_idx++ )
  {
    float3 light_dir  = float3( point_lights[light_idx].Position ) - ws_position.xyz;
    float  light_dist = length( light_dir );

    if ( light_dist > point_lights[light_idx].Range ) continue;

    light_dir          /= light_dist;                         // Normalization

    float  attenuation  = 1.0f / ( light_dist * light_dist ); // TODO: Controlled Attenuation
    float3 radiance =
        point_lights[light_idx].Intensity * UnpackColor32( point_lights[light_idx].Color ).rgb * attenuation;

    point_contrib += brdf.Evaluate( radiance, view_dir, light_dir );
  }

  return point_contrib;
}

float3 CalcDirLightContrib( float4 ws_position, float3 view_dir, in BRDFCookTorranceGGX brdf )
{
  if ( !IsValidHandle( g_DirLights ) ) return 0.0f;

  StructuredBuffer<DirLight> dir_lights  = ResourceDescriptorHeap[g_DirLights];

  float3                     dir_contrib = 0.0f;
  int                        light_idx   = 0;
  for ( ; light_idx < g_ShadowDirLightCount; light_idx++ )
  {
    // Lightdir pre-normalized
    float3 light_dir = dir_lights[light_idx].Direction;
    float  intensity = dir_lights[light_idx].Intensity;

    int    cascade   = 0;
    [unroll] for ( int i = 0; i < NUM_CASCADES; i++ )
    {
      if ( PointInsideSphere( ws_position.xyz, dir_lights[light_idx].CascadesSph[i] ) )
      {
        cascade = i;
      }
    }

    float4 ls_position  = mul( dir_lights[light_idx].LightSpaceMat[cascade], ws_position );
    ls_position        /= ls_position.w;

    ls_position.xy      = float2( 0.5f, -0.5f ) * ls_position.xy + 0.5f; // must invert y

    // Shadow test
    Texture2DArray<float> shadow_map = ResourceDescriptorHeap[dir_lights[light_idx].ShadowIdx];
    float  shadowing = shadow_map.SampleCmp( g_ShadowSampler, float3( ls_position.xy, cascade ), ls_position.z );

    float3 radiance  = intensity * UnpackColor32( dir_lights[light_idx].Color ).rgb;

    // Expects direction *to* light.
    dir_contrib += shadowing * brdf.Evaluate( radiance, view_dir, -light_dir );
  }

  for ( ; light_idx < g_DirLightCount; light_idx++ )
  {
    // Lightdir pre-normalized
    float3 light_dir = dir_lights[light_idx].Direction;
    float  intensity = dir_lights[light_idx].Intensity;

    float3 radiance  = intensity * UnpackColor32( dir_lights[light_idx].Color ).rgb;

    // Expects direction *to* light.
    dir_contrib += brdf.Evaluate( radiance, view_dir, -light_dir );
  }

  return dir_contrib;
}

#endif
