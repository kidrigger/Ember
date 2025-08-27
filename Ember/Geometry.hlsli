#ifndef GEOMETRY_HLSLI_
#define GEOMETRY_HLSLI_

#include "Colors.hlsli"
#include "Quantization.hlsli"

struct Vertex
{
  half4         Position;    // 08
  uint          Normal;      // 12
  uint          Tangent;     // 16
  PackedColor32 Color;       // 20
  half2         TexCoord[2]; // 28
  uint          Padding0;    // 32

  float4        GetPosition()
  {
    return Position;
  }

  float4 GetNormal()
  {
    return float4( 2.0f * UnpackR10G10B10A2Unorm( Normal ).xyz - 1.0f, 0.0f );
  }

  float4 GetTangent()
  {
    return 2.0f * UnpackR10G10B10A2Unorm( Tangent ) - 1.0f;
  }

  float4 GetColor()
  {
    return UnpackColor32( Color );
  }

  float2 GetTexCoord( uint idx )
  {
    return TexCoord[idx];
  }
};

struct Transform
{
  float4x4 Model;
  float4x4 InvModel;
};

struct Meshlet
{
  uint  VertexOffset;
  uint  TriangleOffset;
  uint  VertexCount;
  uint  TriangleCount;
  half4 BoundingSphere; // xyz = center, w = radius
};

struct MeshDraw
{
  uint  FirstTransform;
  uint  TransformCount;
  uint  FirstVertex;
  uint  FirstMeshlet;
  uint  MeshletCount;
  ResID MeshletBuffer;
  ResID MeshletTriangleBuffer;
  ResID MeshletIndexBuffer;
  ResID VertexBuffer;
  ResID ShadowVertexBuffer;
  MatID Material;
  uint  Pad0;
};

struct DrawList
{
  ResID Transforms;
  ResID MeshDraws;
  uint  MeshDrawCount;
};


#endif
