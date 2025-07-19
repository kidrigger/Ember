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

VSOutput TriangleVS(
  float3 position : POSITION,
  float3 color : COLOR)
{
  VSOutput OUT;
  float3 pos = mul(g_Model, float4(position, 1.0f)).xyz;
  OUT.Position = float4(pos.xy, pos.z + 0.5f, 1.0f);
  OUT.Color = float4(color, 1.0f);
  return OUT;
}