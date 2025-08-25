#ifndef OMNI_SHADER_HLSLI_
#define OMNI_SHADER_HLSLI_

#include "Bindless.hlsli"

cbuffer QuickTransforms : register( b0 )
{
  float4x4 g_Transform;
  float3   g_LightPosition;
  float    g_FarPlane;
  ResID    g_ProjViewID;
}

struct ProjectionTransforms
{
  float4x4 Views[6];
};

struct VSOut
{
  float4 ScreenPosition : SV_POSITION;
  float4 WorldPosition : POSITION;
  uint   Layer : SV_RENDERTARGETARRAYINDEX;
};

struct FSOut
{
  float Depth : SV_Depth;
};

typedef VSOut FSIn;

#endif
