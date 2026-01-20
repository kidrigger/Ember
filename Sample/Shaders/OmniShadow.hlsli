#ifndef OMNI_SHADER_HLSLI_
#define OMNI_SHADER_HLSLI_

#include "Bindless.hlsli"
#include "Geometry.hlsli"
#include "Math.hlsli"

const static float        kNearPlane = 0.01f;

ConstantBuffer<DrawBatch> g_DrawBatch : register( b0 );

cbuffer                   QuickTransforms : register( b1 )
{
  float3 g_LightPosition;
  float  g_FarPlane;
}

static const float4 kViewOrientations[] = {
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
