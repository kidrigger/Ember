#ifndef OMNI_SHADER_HLSLI_
#define OMNI_SHADER_HLSLI_

cbuffer QuickTransforms : register( b0 )
{
  float4x4 g_ProjView;
  float4x4 g_Transform;
  float3   g_LightPosition;
  float    g_FarPlane;
}

struct VSOut
{
  float4 ScreenPosition : SV_POSITION;
  float4 WorldPosition : POSITION;
};

struct FSOut
{
  float Depth : SV_Depth;
};

typedef VSOut FSIn;

#endif
