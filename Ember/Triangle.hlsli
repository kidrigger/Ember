#include "ColorSpace.hlsli"

const static uint kInvalidIndex = 0xFFFFFFFF;

bool              IsValidHandle( uint handle )
{
  return handle != kInvalidIndex;
}

typedef uint Color32;
typedef uint RID;
typedef uint SamplerID;

float4       UnpackColor32( Color32 value )
{
  uint a = ( value & 0xFF000000 ) >> 24;
  uint b = ( value & 0x00FF0000 ) >> 16;
  uint g = ( value & 0x0000FF00 ) >> 8;
  uint r = value & 0x000000FF;
  return float4( r, g, b, a ) / 255.0f;
}

struct Camera
{
  float4x4 Projection;
  float4x4 View;
  float4   Position;
};

struct PointLight
{
  float3  Position;    // 12
  float   Range;       // 16
  Color32 Color;       // 20
  float   Intensity;   // 24
  float   Attenuation; // 28
  float   Padding0;    // 32
};

struct Material
{
  RID       BaseColorTextureIndex;  // 04
  RID       NormalTextureIndex;     // 08
  RID       MetalRoughTextureIndex; // 12
  RID       EmissiveTextureIndex;   // 16
  SamplerID SamplerIndex;           // 20
  Color32   BaseColorFactor;        // 24
  Color32   EmissiveFactor;         // 28
  float     EmissiveStrength;       // 32
  float     Metal;                  // 36
  float     Rough;                  // 40
  float     AlphaCutoff;            // 44
  float     Pad0;                   // 48

  Texture2D GetBaseColorTexture()
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
