#ifndef DIR_SHADOW_HLSLI_
#define DIR_SHADOW_HLSLI_

#include "Bindless.hlsli"
#include "Camera.hlsli"
#include "Geometry.hlsli"
#include "LightData.hlsli"

ConstantBuffer<DrawBatch> g_DrawBatch : register( b0 );

//
cbuffer QuickTransforms : register( b1 )
{
  ResID g_LightData;
  uint  g_LightIdx;
  ResID g_Camera;
}

cbuffer LightCullParameters : register( b2 )
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
