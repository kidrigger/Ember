#include "Triangle.hlsli"

cbuffer BindlessIndex : register(b0, space0)
{
  uint g_VertexBufferIndex;
}
cbuffer Transform : register(b1, space0)
{
  float4x4 g_Model;
}

struct Vertex
{
  float3 Position;
  float3 Color;
};


VSOutput TriangleVS(uint index : SV_VERTEXID)
{
  VSOutput OUT;

  StructuredBuffer<Vertex> vbo = ResourceDescriptorHeap[g_VertexBufferIndex];

  Vertex vertex = vbo[index];

  float3 pos = mul(g_Model, float4(vertex.Position, 1.0f)).xyz;
  OUT.Position = float4(pos.xy, pos.z + 0.5f, 1.0f);
  OUT.Color = float4(vertex.Color, 1.0f);
  return OUT;
}
