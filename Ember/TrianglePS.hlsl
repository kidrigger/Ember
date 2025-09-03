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


float4 TrianglePS( PSIn IN ) : SV_TARGET0
{
  StructuredBuffer<Material> materials = ResourceDescriptorHeap[g_Materials];
  Material                   mat       = materials[NonUniformResourceIndex( IN.Material )];

  float4                     albedo    = IN.Color * mat.GetAlbedo( IN.TexCoord[0], g_DefaultSampler );
  float3 normal      = mat.GetNormal( IN.Normal, IN.Tangent, IN.Position.xyz, IN.TexCoord[0], g_DefaultSampler );
  float2 metal_rough = mat.GetMetalRough( IN.TexCoord[0], g_DefaultSampler );
  float3 emissive    = mat.GetEmissive( IN.TexCoord[0], g_DefaultSampler );

  ConstantBuffer<Camera> camera   = ResourceDescriptorHeap[g_Camera];
  float3                 view_dir = normalize( camera.Position.xyz - IN.Position.xyz );

  BRDFCookTorranceGGX    brdf;
  brdf.Albedo          = albedo.xyz;
  brdf.Metallic        = metal_rough.r;
  brdf.Normal          = normal.xyz;
  brdf.Roughness       = metal_rough.g;
  brdf.F0              = lerp( 0.04f, albedo.rgb, metal_rough.x );
  brdf.Occlusion       = 1.0f;

  float3 point_contrib = 0.0f;
  if ( IsValidHandle( g_PointLights ) )
  {
    StructuredBuffer<PointLight> point_lights = ResourceDescriptorHeap[g_PointLights];

    int                          light_idx    = 0;
    for ( ; light_idx < g_ShadowPointLightCount; light_idx++ )
    {
      float3 light_dir  = float3( point_lights[light_idx].Position ) - IN.Position.xyz;
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
      float3 light_dir  = float3( point_lights[light_idx].Position ) - IN.Position.xyz;
      float  light_dist = length( light_dir );

      if ( light_dist > point_lights[light_idx].Range ) continue;

      light_dir          /= light_dist;                         // Normalization

      float  attenuation  = 1.0f / ( light_dist * light_dist ); // TODO: Controlled Attenuation
      float3 radiance =
          point_lights[light_idx].Intensity * UnpackColor32( point_lights[light_idx].Color ).rgb * attenuation;

      point_contrib += brdf.Evaluate( radiance, view_dir, light_dir );
    }
  }

  float3 dir_contrib = 0.0f;
  if ( IsValidHandle( g_DirLights ) )
  {
    StructuredBuffer<DirLight> dir_lights = ResourceDescriptorHeap[g_DirLights];

    int                        light_idx  = 0;
    for ( ; light_idx < g_ShadowDirLightCount; light_idx++ )
    {
      // Lightdir pre-normalized
      float3 light_dir = dir_lights[light_idx].Direction;
      float  intensity = dir_lights[light_idx].Intensity;

      int    cascade   = 0;
      [unroll] for ( int i = 0; i < NUM_CASCADES; i++ )
      {
        if ( PointInsideSphere( IN.Position.xyz, dir_lights[light_idx].CascadesSph[i] ) )
        {
          cascade = i;
        }
      }

      float4 ls_position  = mul( dir_lights[light_idx].LightSpaceMat[cascade], IN.Position );
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
  }

  float3 ambient_contrib = GetAmbientInfluence( brdf, view_dir );

  float3 total_contrib   = emissive + point_contrib + dir_contrib + ambient_contrib;

  return float4( LinearToSrgb( total_contrib ), albedo.a );
}
