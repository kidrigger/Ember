#ifndef OMNI_SHADER_HLSLI_
#define OMNI_SHADER_HLSLI_

#include "Bindless.hlsli"
#include "Geometry.hlsli"

const static float kNearPlane = 0.01f;

cbuffer            QuickTransforms : register( b0 )
{
  DrawList g_DrawList;
  float3   g_LightPosition;
  float    g_FarPlane;
  ResID    g_ProjViewID;
}

struct ProjectionTransforms
{
  float4x4 Views[6];
};

struct MeshletPayload
{
  uint MeshletID[192];
  uint ViewID[192];
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
