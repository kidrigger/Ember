#include "Triangle.hlsli"

const static float3 kPoints[] =
{
  float3(0.0f, 0.5f, 0.0f),
  float3(0.5f, -0.5f, 0.0f),
  float3(-0.5f, -0.5f, 0.0f),
};

const static float3 kColors[] =
{
  float3(0.0f, 0.0f, 1.0f),
  float3(0.0f, 1.0f, 0.0f),
  float3(1.0f, 0.0f, 0.0f),
};

cbuffer Transform : register(b0, space0)
{
  float4x4 g_Model;
}

VSOutput TriangleVS(uint idx : SV_VERTEXID)
{
  VSOutput OUT;
  OUT.Position = float4(mul(g_Model, float4(kPoints[idx], 1.0f)).xy, 0.0f, 1.0f);
  OUT.Color = float4(kColors[idx], 1.0f);
  return OUT;
}