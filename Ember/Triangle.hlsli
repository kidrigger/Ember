#include "Bindless.hlsli"
#include "Colors.hlsli"

struct Camera
{
  float4x4 Projection;
  float4x4 View;
  float4   Position;
};

struct PointLight
{
  float3        Position;    // 12
  float         Range;       // 16
  PackedColor32 Color;       // 20
  float         Intensity;   // 24
  float         Attenuation; // 28
  float         Padding0;    // 32
};

struct Material
{
  RID           BaseColorTextureIndex;  // 04
  RID           NormalTextureIndex;     // 08
  RID           MetalRoughTextureIndex; // 12
  RID           EmissiveTextureIndex;   // 16
  SamplerID     SamplerIndex;           // 20
  PackedColor32 BaseColorFactor;        // 24
  PackedColor32 EmissiveFactor;         // 28
  float         EmissiveStrength;       // 32
  float         Metal;                  // 36
  float         Rough;                  // 40
  float         AlphaCutoff;            // 44
  float         Pad0;                   // 48

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

  SamplerState GetSampler()
  {
    return SamplerDescriptorHeap[SamplerIndex];
  }
};

cbuffer BindlessIndex : register( b0, space0 )
{
  RID  g_Camera;
  RID  g_PointLights;
  uint g_PointLightCount;
}

cbuffer Transform : register( b1, space0 )
{
  float4x4 g_Model;
  float4x4 g_InvModel;
}

cbuffer MaterialInfo : register( b2, space0 )
{
  Material g_Material;
}

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
