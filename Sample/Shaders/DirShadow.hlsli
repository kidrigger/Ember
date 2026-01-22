#ifndef DIR_SHADOW_HLSLI_
#define DIR_SHADOW_HLSLI_

#include "Bindless.hlsli"
#include "Camera.hlsli"
#include "DebugConfig.hlsli"
#include "Geometry.hlsli"
#include "LightData.hlsli"

cbuffer FrameConstants : register( b0 )
{
  Camera      g_Camera;
  LightInfo   g_Lights;
  DebugConfig g_Debug;
}

ConstantBuffer<DrawBatch> g_DrawBatch : register( b1 );

uint                      g_LightIdx : register( b2 );

cbuffer                   LightCullParameters : register( b3 )
{
  float4 g_CullParams[NUM_CASCADES];
}

struct MeshletPayload
{
  uint MeshletID[32 * NUM_CASCADES];
  uint ViewID[32 * NUM_CASCADES];
  uint DrawCmdID;
};

struct MSVertexOut
{
  float4 ScreenPosition : SV_POSITION;
  float4 WorldPosition : POSITION;
};

struct MSPrimitiveOut
{
  uint RTArrayIdx : SV_RENDERTARGETARRAYINDEX;
};

struct PSOut
{
  float Depth : SV_Depth;
};

struct PSIn
{
  float4 WorldPosition : POSITION;
};

#endif
