#ifndef TRIANGLE_HLSLI_
#define TRIANGLE_HLSLI_

#include "Bindless.hlsli"
#include "Camera.hlsli"
#include "Colors.hlsli"
#include "DebugConfig.hlsli"
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

cbuffer                   FrameConstants : register( b1, space0 )
{
  Camera      g_Camera;
  LightInfo   g_Lights;
  Environment g_Env;
  float2      g_RTSize;
  float2      g_Padding;
  DebugConfig g_Debug;
}

cbuffer SSAOIn : register( b2, space0 )
{
  ResID g_AO;
}

SamplerState           g_DefaultSampler : register( s0, space0 );
SamplerState           g_ClampedSampler : register( s1, space0 );
SamplerComparisonState g_ShadowSampler : register( s2, space0 );

#endif
