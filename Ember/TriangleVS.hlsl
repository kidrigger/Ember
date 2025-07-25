#include "Triangle.hlsli"

cbuffer Transform : register(b1, space0)
{
  float4x4 g_Model;
}

struct VSInput
{
  float3 Position : POSITION;
  float3 Color : COLOR;
  float2 TexCoord0 : TEX_COORD0;
};

struct Camera
{
  float4x4 Projection;
  float4x4 View;
};

VSOut TriangleVS(VSInput IN)
{
  VSOut OUT;

  ConstantBuffer<Camera> camera = ResourceDescriptorHeap[g_CameraIndex];

  float4 pos = mul(g_Model, float4(IN.Position, 1.0f));
  pos = mul(camera.View, pos);
  pos = mul(camera.Projection, pos);
  OUT.Position = pos;
  OUT.Color = float4(IN.Color, 1.0f);
  OUT.TexCoord = IN.TexCoord0;
  return OUT;
}
