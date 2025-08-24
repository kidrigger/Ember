#ifndef TRIANGLE_HLSLI_
#define TRIANGLE_HLSLI_

#include "Bindless.hlsli"
#include "Colors.hlsli"
#include "LightData.hlsli"
#include "Quantization.hlsli"

struct Camera
{
  float4x4 Projection;
  float4x4 InvProj;
  float4x4 View;
  float4x4 InvView;
  float4   Position;
};

struct Material
{
  RID           BaseColorTextureIndex;  // 04
  RID           NormalTextureIndex;     // 08
  RID           MetalRoughTextureIndex; // 12
  RID           EmissiveTextureIndex;   // 16
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

struct Meshlet
{
  uint VertexOffset;
  uint TriangleOffset;
  uint VertexCount;
  uint TriangleCount;
};

struct Environment
{
  RID Skybox;
  RID DiffuseIrradiance;
  RID PrefilterMap;
  RID BrdfLUT;
};

cbuffer Transform : register( b0, space0 )
{
  float4x4 g_Model;
  float4x4 g_InvModel;
}

cbuffer DrawInfo : register( b1, space0 )
{
  RID  g_MaterialIdx;
  RID  g_VertexBufferIdx;
  uint g_FirstVertex;
  RID  g_MeshletBufferIdx;
  RID  g_MeshletTrianglesIdx;
  RID  g_MeshletVerticesIdx;
  uint g_FirstMeshlet;
  uint g_FirstIndex;
}

cbuffer BindlessIndex : register( b2, space0 )
{
  RID  g_Materials;
  RID  g_Camera;
  RID  g_PointLights;
  uint g_PointLightCount;
  uint g_ShadowPointLightCount;
  RID  g_DirLights;
  uint g_DirLightCount;
  uint g_ShadowDirLightCount;
}

cbuffer EnvironmentBlock : register( b3, space0 )
{
  Environment g_Env;
}

SamplerState           g_DefaultSampler : register( s0, space0 );
SamplerState           g_ClampedSampler : register( s1, space0 );
SamplerComparisonState g_ShadowSampler : register( s2, space0 );

struct Vertex
{
  half4         Position;    // 08
  uint          Normal;      // 12
  uint          Tangent;     // 16
  PackedColor32 Color;       // 20
  half2         TexCoord[2]; // 28
  uint          Padding0;    // 32

  float4        GetPosition()
  {
    return Position;
  }

  float4 GetNormal()
  {
    return float4( 2.0f * UnpackR10G10B10A2Unorm( Normal ).xyz - 1.0f, 0.0f );
  }

  float4 GetTangent()
  {
    return 2.0f * UnpackR10G10B10A2Unorm( Tangent ) - 1.0f;
  }

  float4 GetColor()
  {
    return UnpackColor32( Color );
  }

  float2 GetTexCoord( uint idx )
  {
    return TexCoord[idx];
  }
};

struct VSOut
{
  float4 ScreenPosition : SV_POSITION;
  float4 Position : POSITION;
  float3 Normal : NORMAL;
  float  LinearDepth : LINEAR_DEPTH;
  float4 Tangent : TANGENT;
  float4 Color : COLOR;
  float2 TexCoord[2] : TEXCOORD;
};

typedef VSOut FSIn;

#endif
