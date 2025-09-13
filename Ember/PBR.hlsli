#ifndef PBR_HLSLI_
#define PBR_HLSLI_

#include "Environment.hlsli"
#include "LightData.hlsli"
#include "Math.hlsli"
#include "Utility.hlsli"

float TrowbridgeReitzGGX( float n_dot_h, float roughness )
{
  float alpha       = roughness * roughness;
  float alpha2      = alpha * alpha;
  float n_dot_h_2   = n_dot_h * n_dot_h;

  float numerator   = alpha2;
  float denominator = n_dot_h_2 * ( alpha2 - 1.0f ) + 1.0f;
  denominator       = kPi * denominator * denominator;

  return numerator / denominator;
}

float PBRGeometrySchlickGGX( float n_dot_v, float roughness )
{
  float r           = roughness + 1.0f;
  float k           = ( r * r ) / 8.0f;

  float numerator   = n_dot_v;
  float denominator = n_dot_v * ( 1.0f - k ) + k;

  return numerator / denominator;
}

float IBLGeometrySchlickGGX( float n_dot_v, float roughness )
{
  float r = roughness;
  // (Rough + 1)^2 / 8 for Punctual Lights
  // Rough^2 / 2 for IBL
  float k           = ( r * r ) / 2.0f;

  float numerator   = n_dot_v;
  float denominator = n_dot_v * ( 1.0f - k ) + k;

  return numerator / denominator;
}

float IBLGeometrySmith( float n_dot_v, float n_dot_l, float roughness )
{
  float ggx1 = IBLGeometrySchlickGGX( n_dot_v, roughness );
  float ggx2 = IBLGeometrySchlickGGX( n_dot_l, roughness );

  return ggx1 * ggx2;
}

float PBRGeometrySmith( float n_dot_v, float n_dot_l, float roughness )
{
  float ggx1 = PBRGeometrySchlickGGX( n_dot_v, roughness );
  float ggx2 = PBRGeometrySchlickGGX( n_dot_l, roughness );

  return ggx1 * ggx2;
}

// https://en.wikipedia.org/wiki/Schlick%27s_approximation
float3 FresnelSchlick( float cosine, float3 f_0 )
{
  return f_0 + ( 1.0f - f_0 ) * pow( clamp( 1.0f - cosine, 0.0f, 1.0f ), 5.0f ); // Clamp to avoid artifacts.
}

// Sebastian Lagarde
float3 FresnelSchlickRoughness( float cosine, float3 f_0, float roughness )
{
  return f_0 + ( max( ( 1.0f - roughness ).xxx, f_0 ) - f_0 ) *
                   pow( clamp( 1.0f - cosine, 0.0f, 1.0f ), 5.0f ); // Clamp to avoid artifacts.
}

struct BRDFCookTorranceGGX
{
  float3 Albedo;
  float  Metallic;
  float3 Normal;
  float  Roughness;
  float3 F0;
  float  Occlusion;

  float3 Evaluate( float3 radiance, float3 view_dir, float3 light_dir )
  {
    float3 halfway        = normalize( view_dir + light_dir );

    float  cosine_factor  = max( dot( halfway, view_dir ), 0.0f );
    float  n_dot_h        = max( dot( Normal, halfway ), 0.0f );
    float  n_dot_v        = max( dot( Normal, view_dir ), 0.0f );
    float  n_dot_l        = max( dot( Normal, light_dir ), 0.0f );

    float  normal_dist    = TrowbridgeReitzGGX( n_dot_h, Roughness );
    float  geometry       = PBRGeometrySmith( n_dot_v, n_dot_l, Roughness );
    float3 fresnel        = FresnelSchlickRoughness( cosine_factor, F0, Roughness );

    float3 numerator      = ( normal_dist * geometry ) * fresnel;
    float  denominator    = 4.0f * n_dot_v * n_dot_l;
    float3 specular       = numerator / ( denominator + 0.00001f );

    float3 specular_part  = fresnel;
    float3 diffuse_part   = 1.0f - specular_part;

    diffuse_part         *= 1.0f - Metallic;

    return n_dot_l * radiance * ( diffuse_part * Albedo / kPi + specular ) * Occlusion;
  }
};

float3 GetAmbientInfluence(
    in Environment         env,
    in BRDFCookTorranceGGX brdf,
    float3                 view_dir,
    SamplerState           default_sampler,
    SamplerState           clamped_lut_sampler )
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
  float3 prefiltered_color  = env.SamplePrefiltered( reflection_dir, brdf.Roughness, default_sampler ).rgb;
  float2 env_brdf           = env.SampleBrdfLut( n_dot_v, brdf.Roughness, clamped_lut_sampler );
  float3 specular           = prefiltered_color * ( specular_part * env_brdf.x + env_brdf.y );

  float3 diffuse            = brdf.Albedo * env.SampleIrradiance( brdf.Normal, default_sampler );

  return ( diffuse_part * diffuse + specular ) * brdf.Occlusion;
}

float3 GetAmbientInfluence(
    in Environment         env,
    in BRDFCookTorranceGGX brdf,
    float3                 view_dir,
    SamplerState           default_sampler,
    SamplerState           clamped_lut_sampler,
    bool                   use_diffuse,
    bool                   use_spec )
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
    float3 prefiltered_color = env.SamplePrefiltered( reflection_dir, brdf.Roughness, default_sampler ).rgb;
    float2 env_brdf          = env.SampleBrdfLut( n_dot_v, brdf.Roughness, clamped_lut_sampler );
    specular                 = prefiltered_color * ( specular_part * env_brdf.x + env_brdf.y );
  }
  if ( use_diffuse )
  {
    diffuse = brdf.Albedo * env.SampleIrradiance( brdf.Normal, default_sampler );
  }

  return ( diffuse_part * diffuse + specular ) * brdf.Occlusion;
}

template <typename TBrdf>
float3 CalcShadowingLightContrib(
    in DirLight dir_light, in TBrdf brdf, float4 ws_position, float3 view_dir, SamplerComparisonState shadow_sampler )
{
  Texture2DArray<float> shadow_map = ResourceDescriptorHeap[dir_light.ShadowIdx];

  int                   cascade    = 0;
  [unroll] for ( int i = 0; i < NUM_CASCADES; i++ )
  {
    if ( PointInsideSphere( ws_position.xyz, dir_light.CascadesSph[i] ) )
    {
      cascade = i;
    }
  }

  float4 ls_position  = mul( dir_light.LightSpaceMat[cascade], ws_position );
  ls_position        /= ls_position.w;

  ls_position.xy      = float2( 0.5f, -0.5f ) * ls_position.xy + 0.5f; // must invert y

  // Shadow test
  float shadowing = shadow_map.SampleCmp( shadow_sampler, float3( ls_position.xy, cascade ), ls_position.z );

  // Expects direction *to* light.
  return shadowing * brdf.Evaluate( dir_light.GetRadiance(), view_dir, -dir_light.Direction );
}

template <typename TBrdf>
float3 CalcLightContrib( in DirLight dir_light, in TBrdf brdf, float4 ws_position, float3 view_dir )
{
  // Expects direction *to* light.
  return brdf.Evaluate( dir_light.GetRadiance(), view_dir, -dir_light.Direction );
}

template <typename TBrdf>
float3 CalcShadowingLightContrib(
    in PointLight          point_light,
    in TBrdf               brdf,
    float4                 ws_position,
    float3                 view_dir,
    SamplerComparisonState shadow_sampler )
{
  float3 light_dir  = float3( point_light.Position ) - ws_position.xyz;
  float  light_dist = length( light_dir );

  if ( light_dist > point_light.Range ) return 0.0f;

  // Shadow test
  TextureCube<float> shadow_map = ResourceDescriptorHeap[point_light.ShadowIdx];
  float shadowing = shadow_map.SampleCmpLevelZero( shadow_sampler, -light_dir, light_dist / point_light.Range );

  // Smooth attenuation from [karis13]
  float attenuation =
      pow( saturate( 1 - pow( light_dist / point_light.Range, 4.0f ) ), 2.0f ) / ( light_dist * light_dist + 1.0f );
  float3 radiance  = attenuation * point_light.GetRadiance();

  light_dir       /= light_dist; // Normalization
  return shadowing * brdf.Evaluate( radiance, view_dir, light_dir );
}

template <typename TBrdf>
float3 CalcLightContrib( in PointLight point_light, in TBrdf brdf, float4 ws_position, float3 view_dir )
{
  float3 light_dir  = float3( point_light.Position ) - ws_position.xyz;
  float  light_dist = length( light_dir );

  if ( light_dist > point_light.Range ) return 0.0f;

  // Smooth attenuation from [karis13]
  float attenuation =
      pow( saturate( 1 - pow( light_dist / point_light.Range, 4.0f ) ), 2.0f ) / ( light_dist * light_dist + 1.0f );
  float3 radiance  = attenuation * point_light.GetRadiance();

  light_dir       /= light_dist; // Normalization
  return brdf.Evaluate( radiance, view_dir, light_dir );
}

#endif
