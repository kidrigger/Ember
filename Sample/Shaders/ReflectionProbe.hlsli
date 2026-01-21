#ifndef REFLECTION_PROBE_HLSLI_
#define REFLECTION_PROBE_HLSLI_

#include "Bindless.hlsli"
#include "Environment.hlsli"
#include "Geometry.hlsli"
#include "Math.hlsli"

const static float        kNearPlane = 0.01f;

ConstantBuffer<DrawBatch> g_DrawBatch : register( b0 );

cbuffer                   BindlessIndex : register( b1 )
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

ConstantBuffer<Environment> g_Env : register( b2 );

cbuffer                     QuickTransforms : register( b3 )
{
  float4 g_ProbeInfo;
}

SamplerState           g_DefaultSampler : register( s0, space0 );
SamplerState           g_ClampedSampler : register( s1, space0 );
SamplerComparisonState g_ShadowSampler : register( s2, space0 );

static const float4    kViewOrientations[] = {
  float4( 0, -INV_ROOT2, 0, INV_ROOT2 ),
  float4( 0, INV_ROOT2, 0, INV_ROOT2 ),
  float4( INV_ROOT2, 0, 0, INV_ROOT2 ),
  float4( -INV_ROOT2, 0, 0, INV_ROOT2 ),
  float4( 0, 0, 0, 1 ),
  float4( 0, 1, 0, 0 ),
};

struct MeshletPayload
{
  uint MeshletID[192];
  uint ViewID[192];
  uint DrawCmdID;
};

struct MSVertexOut
{
  float4 ScreenPosition : SV_POSITION;
  float4 WorldPosition : POSITION;
  float3 Normal : NORMAL;
  float4 Tangent : TANGENT;
  float4 Color : COLOR;
  float2 TexCoord[2] : TEXCOORD;
};

struct MSPrimitiveOut
{
  MatID Material : MATERIAL;
  uint  RTArrayIdx : SV_RENDERTARGETARRAYINDEX;
};

struct PSOut
{
  float Depth : SV_Depth;
};

struct PSIn
{
  float4 ScreenPosition : SV_POSITION;
  float4 Position : POSITION;
  float3 Normal : NORMAL;
  float4 Tangent : TANGENT;
  float4 Color : COLOR;
  float2 TexCoord[2] : TEXCOORD;
  MatID  Material : MATERIAL;
};

#endif
