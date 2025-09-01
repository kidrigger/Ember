#ifndef TRIANGLE_HLSLI_
#define TRIANGLE_HLSLI_

#include "Bindless.hlsli"
#include "Camera.hlsli"
#include "Colors.hlsli"
#include "Geometry.hlsli"
#include "LightData.hlsli"
#include "Quantization.hlsli"

struct Material
{
  ResID         BaseColorTextureIndex;  // 04
  ResID         NormalTextureIndex;     // 08
  ResID         MetalRoughTextureIndex; // 12
  ResID         EmissiveTextureIndex;   // 16
  PackedColor32 BaseColorFactor;        // 20
  PackedColor32 EmissiveFactor;         // 24
  float         EmissiveStrength;       // 28
  float         Metal;                  // 32
  float         Rough;                  // 36
  float         AlphaCutoff;            // 40
  float         Pad0;                   // 44
  float         Pad1;                   // 48

  float4        GetAlbedo( float2 in_texcoord, SamplerState texture_sampler )
  {
    float4 albedo = UnpackColor32( BaseColorFactor );
    if ( IsValidHandle( BaseColorTextureIndex ) )
    {
      Texture2D texture = ResourceDescriptorHeap[BaseColorTextureIndex];
      return albedo * texture.Sample( texture_sampler, in_texcoord );
    }
    return albedo;
  }


  float3 GetNormal(
      float3 in_normal, float4 in_tangent, float3 in_position, float2 in_texcoord, SamplerState texture_sampler )
  {
    float3 normal = normalize( in_normal );
    if ( IsValidHandle( NormalTextureIndex ) )
    {
      Texture2D texture   = ResourceDescriptorHeap[NormalTextureIndex];
      float3    normal_ts = texture.Sample( texture_sampler, in_texcoord ).rgb;
      normal_ts           = normalize( 2.0f * normal_ts - 1.0f );

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
    if ( IsValidHandle( MetalRoughTextureIndex ) )
    {
      Texture2D texture = ResourceDescriptorHeap[MetalRoughTextureIndex];
      return texture.Sample( texture_sampler, in_texcoord ).bg * float2( Metal, Rough );
    }
    return float2( Metal, Rough );
  }

  float3 GetEmissive( float2 in_texcoord, SamplerState texture_sampler )
  {
    float3 emissive = UnpackColor32( EmissiveFactor ).rgb * EmissiveStrength;
    if ( IsValidHandle( EmissiveTextureIndex ) )
    {
      Texture2D texture = ResourceDescriptorHeap[EmissiveTextureIndex];
      return emissive * texture.Sample( texture_sampler, in_texcoord ).rgb;
    }
    return emissive;
  }
};

struct Environment
{
  ResID Skybox;
  ResID DiffuseIrradiance;
  ResID PrefilterMap;
  ResID BrdfLUT;
};

struct MeshletPayload
{
  uint  MeshletID[32];
  uint  Transform;
  uint  FirstVertex;
  uint  FirstMeshlet;
  MatID Material;
};

struct MSVertexOut
{
  float4 ScreenPosition : SV_POSITION;
  float4 Position : POSITION;
  float3 Normal : NORMAL;
  float  LinearDepth : LINEAR_DEPTH;
  float4 Tangent : TANGENT;
  float4 Color : COLOR;
  float2 TexCoord[2] : TEXCOORD;
};

struct PSIn
{
  float4 ScreenPosition : SV_POSITION;
  float4 Position : POSITION;
  float3 Normal : NORMAL;
  float  LinearDepth : LINEAR_DEPTH;
  float4 Tangent : TANGENT;
  float4 Color : COLOR;
  float2 TexCoord[2] : TEXCOORD;
  MatID  Material : MATERIAL;
};

cbuffer DrawListBlock : register( b0, space0 )
{
  DrawList g_DrawList;
}

cbuffer BindlessIndex : register( b1, space0 )
{
  ResID g_Materials;
  ResID g_Camera;
  ResID g_PointLights;
  uint  g_PointLightCount;
  uint  g_ShadowPointLightCount;
  ResID g_DirLights;
  uint  g_DirLightCount;
  uint  g_ShadowDirLightCount;
}

cbuffer EnvironmentBlock : register( b2, space0 )
{
  Environment g_Env;
}

SamplerState           g_DefaultSampler : register( s0, space0 );
SamplerState           g_ClampedSampler : register( s1, space0 );
SamplerComparisonState g_ShadowSampler : register( s2, space0 );

#endif
