#include "Triangle.hlsli"

#include "PBR.hlsli"

float4 GetAlbedo( float2 in_texcoord, SamplerState texture_sampler )
{
  float4 albedo = UnpackColor32( g_Material.BaseColorFactor );
  if ( IsValidHandle( g_Material.BaseColorTextureIndex ) )
  {
    Texture2D texture = g_Material.GetBaseColorTexture();
    return albedo * texture.Sample( texture_sampler, in_texcoord );
  }
  return albedo;
}

float3 GetNormal(
    float3 in_normal, float4 in_tangent, float3 in_position, float2 in_texcoord, SamplerState texture_sampler )
{
  float3 normal = normalize( in_normal );
  if ( IsValidHandle( g_Material.NormalTextureIndex ) )
  {
    float3 normal_ts = g_Material.GetNormalTexture().Sample( texture_sampler, in_texcoord ).rgb;
    normal_ts        = normalize( 2.0f * normal_ts - 1.0f );

    float3 tangent;
    float3 bitangent;

    if ( in_tangent.w == 0.0f )
    {
      float3 q1  = ddx( in_position );
      float3 q2  = ddy( in_position );
      float2 st1 = ddx( in_texcoord );
      float2 st2 = ddy( in_texcoord );

      float  det = ( st1.x * st2.y - st2.x * st1.y );

      tangent    = -( q1 * st2.y - q2 * st1.y ) / det;
      tangent    = tangent - normal * dot( normal, tangent );
      bitangent  = normalize( cross( normal, tangent ) );
    }
    else
    {
      tangent   = normalize( in_tangent.xyz );
      bitangent = in_tangent.w * cross( normal, tangent );
    }
    normal = normalize( tangent * normal_ts.x + bitangent * normal_ts.y + normal * normal_ts.z );
  }

  return normal;
}

float2 GetMetalRough( float2 in_texcoord, SamplerState texture_sampler )
{
  if ( IsValidHandle( g_Material.MetalRoughTextureIndex ) )
  {
    return g_Material.GetMetalRoughTexture().Sample( texture_sampler, in_texcoord ).bg *
           float2( g_Material.Metal, g_Material.Rough );
  }
  return float2( g_Material.Metal, g_Material.Rough );
}

float3 GetEmissive( float2 in_texcoord, SamplerState texture_sampler )
{
  float3 emissive = UnpackColor32( g_Material.EmissiveFactor ).rgb * g_Material.EmissiveStrength;
  if ( IsValidHandle( g_Material.EmissiveTextureIndex ) )
  {
    Texture2D texture = g_Material.GetEmissiveTexture();
    return emissive * texture.Sample( texture_sampler, in_texcoord ).rgb;
  }
  return emissive;
}

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

  float3 f_0                 = 0.04f;
  f_0                        = lerp( f_0, brdf.Albedo, brdf.Metallic );
  float3 specular_part       = FresnelSchlickRoughness( cosine_factor, f_0, brdf.Roughness );
  float3 diffuse_part        = 1.0f - specular_part;

  diffuse_part              *= 1.0f - brdf.Metallic; // Metals don't have diffuse/refractions.

  float3 reflection_dir      = reflect( -view_dir, brdf.Normal );

  float  n_dot_v             = max( dot( brdf.Normal, view_dir ), 0.0f );
  float3 prefiltered_color   = SamplePrefiltered( reflection_dir, brdf.Roughness ).rgb;
  float2 env_brdf            = SampleBrdfLut( n_dot_v, brdf.Roughness );
  float3 specular            = prefiltered_color * ( specular_part * env_brdf.x + env_brdf.y );

  float3 diffuse  = brdf.Albedo * SampleIrradiance( brdf.Normal );
  // #ifdef _DEBUG
  //   if ( ( PushConstant.DebugFlags & USE_DIFFUSE_BIT ) == 0 )
  //   {
  //     DiffuseIrradiance = 0.0f.xxx;
  //   }
  //   if ( ( PushConstant.DebugFlags & USE_SPECULAR_BIT ) == 0 )
  //   {
  //     Specular = 0.0f.xxx;
  //   }
  // #endif

  return ( diffuse_part * diffuse + specular ) * brdf.Occlusion;
}


float4 TrianglePS( FSIn IN ) : SV_TARGET0
{
  if ( !IsValidHandle( g_Material.SamplerIndex ) )
  {
    return float4( 1.0f, 0.0f, 1.0f, 1.0f );
  }
  SamplerState           texture_sampler = g_Material.GetSampler();

  float4                 albedo          = GetAlbedo( IN.TexCoord[0], texture_sampler );
  float3                 normal = GetNormal( IN.Normal, IN.Tangent, IN.Position.xyz, IN.TexCoord[0], texture_sampler );
  float2                 metal_rough = GetMetalRough( IN.TexCoord[0], texture_sampler );
  float3                 emissive    = GetEmissive( IN.TexCoord[0], texture_sampler );

  ConstantBuffer<Camera> camera      = ResourceDescriptorHeap[g_Camera];
  float3                 view_dir    = normalize( camera.Position.xyz - IN.Position.xyz );

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

    for ( int i = 0; i < g_PointLightCount; i++ )
    {
      float3 light_dir   = float3( point_lights[i].Position ) - IN.Position.xyz;
      float  light_dist2 = dot( light_dir, light_dir );

      if ( light_dist2 > point_lights[i].Range ) continue;

      light_dir          /= sqrt( light_dist2 ); // Normalization

      float  attenuation  = 1.0f / light_dist2;  // TODO: Controlled Attenuation
      float3 radiance     = UnpackColor32( point_lights[i].Color ).rgb * attenuation;

      point_contrib      += brdf.Evaluate( radiance, view_dir, light_dir );
    }
  }

  float3 ambient_contrib = GetAmbientInfluence( brdf, view_dir );

  float3 total_contrib   = emissive + point_contrib + ambient_contrib;

  return float4( LinearToSrgb( total_contrib ), albedo.a );
}
