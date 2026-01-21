#ifndef TRIANGLE_HLSLI_
#define TRIANGLE_HLSLI_

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
  uint MeshletID[32];
  uint InstanceIdx;
  uint FirstMeshlet;
};

struct MSVertexOut
{
  float4 ScreenPosition : SV_POSITION;
  float4 Position : POSITION;
  float3 Normal : NORMAL;
  float4 Tangent : TANGENT;
  float4 Color : COLOR;
  float2 TexCoord[2] : TEXCOORD;
};

struct MSPrimitiveOut
{
  float3 MeshletColor : MESHLET_COLOR;
  MatID  Material : MATERIAL;
};

struct PSIn
{
  float4 ScreenPosition : SV_POSITION;
  float4 Position : POSITION;
  float3 Normal : NORMAL;
  float4 Tangent : TANGENT;
  float4 Color : COLOR;
  float2 TexCoord[2] : TEXCOORD;
  float3 MeshletColor : MESHLET_COLOR;
  MatID  Material : MATERIAL;
};

ConstantBuffer<DrawBatch> g_DrawBatch : register( b0, space0 );

cbuffer                   BindlessIndex : register( b1, space0 )
{
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

ConstantBuffer<Environment> g_Env : register( b2, space0 );

SamplerState                g_DefaultSampler : register( s0, space0 );
SamplerState                g_ClampedSampler : register( s1, space0 );
SamplerComparisonState      g_ShadowSampler : register( s2, space0 );

#endif
