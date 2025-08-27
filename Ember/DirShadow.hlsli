#ifndef DIR_SHADOW_HLSLI_
#define DIR_SHADOW_HLSLI_

#include "Bindless.hlsli"
#include "Camera.hlsli"
#include "Geometry.hlsli"
#include "LightData.hlsli"

cbuffer QuickTransforms : register( b0 )
{
  ResID g_Transforms;
  ResID g_MeshDraws;
  uint  g_MeshDrawCount;
  ResID g_LightData;
  uint  g_LightIdx;
  ResID g_Camera;
}

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
