#include "Triangle.hlsli"

cbuffer BindlessIndex : register( b0, space0 )
{
  uint g_CameraIndex;
}

cbuffer Transform : register( b1, space0 )
{
  float4x4 g_Model;
}

struct Vertex
{
  float3 Position : POSITION;
  float3 Color : COLOR;
};

struct Camera
{
  float4x4 Projection;
  float4x4 View;
};

VSOutput TriangleVS( Vertex vertex )
{
  VSOutput               OUT;

  ConstantBuffer<Camera> camera = ResourceDescriptorHeap[g_CameraIndex];

  float4                 pos    = mul( g_Model, float4( vertex.Position, 1.0f ) );
  pos                           = mul( camera.View, pos );
  pos                           = mul( camera.Projection, pos );
  OUT.Position                  = pos;
  OUT.Color                     = float4( vertex.Color, 1.0f );
  return OUT;
}
