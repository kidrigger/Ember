#ifndef DIR_SHADOW_HLSLI_
#define DIR_SHADOW_HLSLI_

#include "Bindless.hlsli"
#include "Camera.hlsli"
#include "Geometry.hlsli"
#include "LightData.hlsli"

cbuffer QuickTransforms : register( b0 )
{
  DrawList g_DrawList;
  ResID    g_LightData;
  uint     g_LightIdx;
  ResID    g_Camera;
}

cbuffer LightCullParameters : register( b1 )
{
  float4 g_CullParams[NUM_CASCADES];
}

struct MeshletPayload
{
  uint MeshletID[32 * NUM_CASCADES];
  uint ViewID[32 * NUM_CASCADES];
  uint MeshDrawID;
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
