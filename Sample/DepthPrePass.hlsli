#ifndef DEPTH_PRE_PASS_HLSLI_
#define DEPTH_PRE_PASS_HLSLI_

#include "Bindless.hlsli"
#include "Camera.hlsli"
#include "Colors.hlsli"
#include "Environment.hlsli"
#include "Geometry.hlsli"
#include "LightData.hlsli"
#include "Material.hlsli"
#include "Quantization.hlsli"

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
  float2 TexCoord[2]    : TEXCOORD;
};

struct MSPrimitiveOut
{
  MatID Material : MATERIAL;
};

struct PSIn
{
  float2 TexCoord[2] : TEXCOORD;
  MatID  Material    : MATERIAL;
};

cbuffer DrawListBlock : register( b0, space0 )
{
  DrawList g_DrawList;
}

cbuffer BindlessIndex : register( b1, space0 )
{
  ResID g_Materials;
  ResID g_Camera;
  ResID g_ConfigID;
  ResID g_PointLights;
  uint  g_ShadowPointLightCount;
  uint  g_PointLightCount;
  ResID g_DirLights;
  uint  g_ShadowDirLightCount;
  uint  g_DirLightCount;
  ResID g_SpotLights;
  uint  g_ShadowSpotLightCount;
  uint  g_SpotLightCount;
}

SamplerState g_DefaultSampler : register( s0 );

#endif
