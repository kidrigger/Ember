#ifndef TRIANGLE_HLSLI_
#define TRIANGLE_HLSLI_

#include "Bindless.hlsli"
#include "Colors.hlsli"

struct Camera
{
  float4x4 Projection;
  float4x4 InvProj;
  float4x4 View;
  float4x4 InvView;
  float4   Position;
};

struct PointLight
{
  float3        Position;    // 12
  float         Range;       // 16
  PackedColor32 Color;       // 20
  float         Intensity;   // 24
  float         Attenuation; // 28
  RID           ShadowIdx;   // 32
};

struct DirLight
{
  float4x4      LightSpaceMat; // 16
  float3        Direction;     // 28
  PackedColor32 Color;         // 32
  float         Intensity;     // 36
  RID           ShadowIdx;     // 40
  uint          Pad0;          // 44
  uint          Pad1;          // 48
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

  Texture2D     GetBaseColorTexture()
  {
    return ResourceDescriptorHeap[BaseColorTextureIndex];
  }

  Texture2D GetNormalTexture()
  {
    return ResourceDescriptorHeap[NormalTextureIndex];
  }

  Texture2D GetMetalRoughTexture()
  {
    return ResourceDescriptorHeap[MetalRoughTextureIndex];
  }

  Texture2D GetEmissiveTexture()
  {
    return ResourceDescriptorHeap[EmissiveTextureIndex];
  }
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

cbuffer MaterialInfo : register( b1, space0 )
{
  Material g_Material;
}

cbuffer BindlessIndex : register( b2, space0 )
{
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

struct VSInput
{
  float3 Position : POSITION;
  float3 Normal : NORMAL;
  float4 Tangent : TANGENT;
  float3 Color : COLOR;
  float2 TexCoord[2] : TEXCOORD;
};

struct VSOut
{
  float4 ScreenPosition : SV_POSITION;
  float4 Position : POSITION;
  float3 Normal : NORMAL;
  float4 Tangent : TANGENT;
  float4 Color : COLOR;
  float2 TexCoord[2] : TEXCOORD;
};

typedef VSOut FSIn;

#endif
