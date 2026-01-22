#include "DebugConfig.hlsli"
#include "TrianglePSCommon.hlsli"

#define RT_MAX_ROUGHNESS 0.2f

cbuffer Probe : register( b3 )
{
  float4 g_ProbeInfo;
  ResID  g_ProbeTex;
}

float3 SampleProbePrefilter( in TextureCube prefilter, float3 direction, float roughness, in SamplerState sam )
{
  const static float kMaxMipLevel          = 5.0f;
  const static float kRoughnessToMipFactor = 2; // we reach max mip at 1/kRougnessToMipMultiplier
  float              mip                   = kMaxMipLevel * saturate( roughness * kRoughnessToMipFactor );
  return prefilter.SampleLevel( sam, direction, mip ).rgb;
}

float3 ParallaxCorrection( in float3 refl_dir, in float3 position, in float4 probe_info )
{
  float3 bound_max = probe_info.xyz + probe_info.www;
  float3 bound_min = probe_info.xyz - probe_info.www;

  float3 t_max     = ( bound_max - position ) / refl_dir;
  float3 t_min     = ( bound_min - position ) / refl_dir;
  float3 t_far     = max( t_max, t_min );
  float  t         = min( t_far.x, min( t_far.y, t_far.z ) );

  float3 intersect = position + refl_dir * t;
  refl_dir         = intersect - probe_info.xyz;

  return refl_dir;
}

float3 GetAmbientInfluenceProbe(
    in TextureCube         prefilter,
    in BRDFCookTorranceGGX brdf,
    float4                 probe_info,
    float3                 position,
    float3                 view_dir,
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
    float n_dot_v            = max( dot( brdf.Normal, view_dir ), 0.0f );
    reflection_dir           = ParallaxCorrection( normalize( reflection_dir ), position, probe_info );
    float3 prefiltered_color = SampleProbePrefilter( prefilter, reflection_dir, brdf.Roughness, g_DefaultSampler ).rgb;
    float2 env_brdf          = g_Env.SampleBrdfLut( n_dot_v, brdf.Roughness, g_ClampedSampler );
    specular                 = prefiltered_color * ( specular_part * env_brdf.x + env_brdf.y );
  }
  if ( use_diffuse )
  {
    diffuse = brdf.Albedo * g_Env.SampleIrradiance( brdf.Normal, g_DefaultSampler );
  }

  return ( diffuse_part * diffuse + specular ) * brdf.Occlusion;
}

float4 TrianglePS( PSIn IN ) : SV_TARGET0
{
  StructuredBuffer<Material> materials = ResourceDescriptorHeap[g_DrawBatch.MaterialBuffer];

  //
  float3   view_dir    = normalize( g_Camera.Position.xyz - IN.Position.xyz );

  Material mat         = materials[NonUniformResourceIndex( IN.Material )];

  float4   albedo      = mat.GetAlbedo( IN.TexCoord, g_DefaultSampler ) * IN.Color;
  float3   normal      = mat.GetNormal( IN.Normal, IN.Tangent, IN.Position.xyz, IN.TexCoord, g_DefaultSampler );
  float2   metal_rough = mat.GetMetalRough( IN.TexCoord, g_DefaultSampler );
  float3   emissive    = mat.GetEmissive( IN.TexCoord, g_DefaultSampler );

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

  if ( IsValidHandle( g_DrawBatch.TopLevelAS ) )
  {
    RayQuery<RAY_FLAG_CULL_BACK_FACING_TRIANGLES> query;
    point_contrib = CalcPointLightContrib( brdf, IN.Position, view_dir, query );
    spot_contrib  = CalcSpotLightContrib( brdf, IN.Position, view_dir, query );
    dir_contrib   = CalcDirLightContrib( brdf, IN.Position, view_dir, query );

    // Ambient
    float cosine_factor =
        max( dot( brdf.Normal, view_dir ), 0.0f ); // Normal instead of Halfway since there's no halfway in ambient.

    float3 f_0            = 0.04f;
    f_0                   = lerp( f_0, brdf.Albedo, brdf.Metallic );
    float3 specular_part  = FresnelSchlickRoughness( cosine_factor, f_0, brdf.Roughness );
    float3 diffuse_part   = 1.0f - specular_part;

    diffuse_part         *= 1.0f - brdf.Metallic; // Metals don't have diffuse/refractions.

    float3 specular       = 0.0f;
    if ( !g_Debug.RemoveSpecularContrib )
    {
      float3 reflection_dir = reflect( -view_dir, brdf.Normal );

      float  n_dot_v        = max( dot( brdf.Normal, view_dir ), 0.0f );
      float3 prefiltered_color;
      if ( IsValidHandle( g_ProbeTex ) )
      {
        TextureCube probe = ResourceDescriptorHeap[g_ProbeTex];
        prefiltered_color = SampleProbePrefilter( probe, reflection_dir, brdf.Roughness, g_DefaultSampler ).rgb;
      }
      else
      {
        prefiltered_color = g_Env.SamplePrefiltered( reflection_dir, brdf.Roughness, g_DefaultSampler ).rgb;
      }

      if ( brdf.Roughness < RT_MAX_ROUGHNESS )
      {
        // TODO: Not sure this is physically accurate.
        // Verify math.
        RaytracingAccelerationStructure tlas = ResourceDescriptorHeap[g_DrawBatch.TopLevelAS];

        RayDesc                         desc;
        desc.Origin    = IN.Position.xyz;
        desc.Direction = reflection_dir;
        desc.TMin      = 0.0001f;
        desc.TMax      = 100.0f;

        query.TraceRayInline( tlas, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, ~0, desc );
        query.Proceed();

        if ( query.CommittedStatus() == COMMITTED_TRIANGLE_HIT )
        {
          uint              instance_id = query.CommittedInstanceID();
          ByteAddressBuffer draws       = ResourceDescriptorHeap[g_DrawBatch.DrawBuffer];
          ByteAddressBuffer ugb         = ResourceDescriptorHeap[g_DrawBatch.GeometryBuffer];
          DrawInstance      instance =
              draws.Load<DrawInstance>( g_DrawBatch.InstancesOffset + DrawInstance_size * instance_id );
          DrawMesh mesh = draws.Load<DrawMesh>( DrawMesh_size * instance.MeshID );

          if ( IsValidHandle( mesh.Material ) )
          {
            Material   refl_mat = materials[mesh.Material];

            float2     bary     = query.CommittedTriangleBarycentrics();
            uint       prim     = query.CommittedPrimitiveIndex();
            uint3      inds     = ugb.Load3( 4 * ( mesh.IndexStart + prim * 3 ) );
            VertexLite v0       = ugb.Load<VertexLite>( VertexLite_size * ( mesh.VertexLiteStart + inds[0] ) );
            VertexLite v1       = ugb.Load<VertexLite>( VertexLite_size * ( mesh.VertexLiteStart + inds[1] ) );
            VertexLite v2       = ugb.Load<VertexLite>( VertexLite_size * ( mesh.VertexLiteStart + inds[2] ) );

            float2     uv[2];
            uv[0] = v0.TexCoord[0] + ( v1.TexCoord[0] - v0.TexCoord[0] ) * bary.x +
                    ( v2.TexCoord[0] - v0.TexCoord[0] ) * bary.y;
            uv[1] = v0.TexCoord[1] + ( v1.TexCoord[1] - v0.TexCoord[1] ) * bary.x +
                    ( v2.TexCoord[1] - v0.TexCoord[1] ) * bary.y;

            prefiltered_color = lerp(
                refl_mat.GetAlbedo( uv, g_DefaultSampler ).rgb,
                prefiltered_color,
                smoothstep( 0.0f, RT_MAX_ROUGHNESS, brdf.Roughness ) );
          }
        }
      }
      float2 env_brdf = g_Env.SampleBrdfLut( n_dot_v, brdf.Roughness, g_ClampedSampler );
      specular        = prefiltered_color * ( specular_part * env_brdf.x + env_brdf.y );
    }

    float3 diffuse = 0.0f;
    if ( !g_Debug.RemoveDiffuseContrib )
    {
      diffuse = brdf.Albedo * g_Env.SampleIrradiance( brdf.Normal, g_DefaultSampler );
    }

    ambient_contrib = ( diffuse_part * diffuse + specular ) * brdf.Occlusion;
  }
  else
  {
    point_contrib = CalcPointLightContrib( brdf, IN.Position, view_dir );
    spot_contrib  = CalcSpotLightContrib( brdf, IN.Position, view_dir );
    dir_contrib   = CalcDirLightContrib( brdf, IN.Position, view_dir );

#ifdef STRIP_DEBUG_CONFIG
    ambient_contrib = GetAmbientInfluence( g_Env, brdf, view_dir, g_DefaultSampler, g_ClampedSampler );
#else
    if ( IsValidHandle( g_ProbeTex ) )
    {
      TextureCube prefil = ResourceDescriptorHeap[g_ProbeTex];
      ambient_contrib    = GetAmbientInfluenceProbe(
          prefil,
          brdf,
          g_ProbeInfo,
          IN.Position.xyz,
          view_dir,
          !g_Debug.RemoveDiffuseContrib,
          !g_Debug.RemoveSpecularContrib );
    }
    else
    {
      ambient_contrib = GetAmbientInfluence(
          g_Env,
          brdf,
          view_dir,
          g_DefaultSampler,
          g_ClampedSampler,
          !g_Debug.RemoveDiffuseContrib,
          !g_Debug.RemoveSpecularContrib );
    }
#endif
  }

  float3 total_contrib = emissive + point_contrib + dir_contrib + spot_contrib + ambient_contrib;

  return float4( total_contrib, 1.0f );
}
