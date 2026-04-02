#ifndef DEPTH_PRE_PASS_HLSLI_
#define DEPTH_PRE_PASS_HLSLI_

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
  float2 TexCoord[2] : TEXCOORD;
};

struct MSPrimitiveOut
{
  MatID Material : MATERIAL;
};

struct PSIn
{
  float2 TexCoord[2] : TEXCOORD;
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

SamplerState g_DefaultSampler : register( s0 );

#endif
