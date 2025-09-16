#ifndef GEOMETRY_HLSLI_
#define GEOMETRY_HLSLI_

#include "Colors.hlsli"
#include "Material.hlsli"
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
  uint  VertexOffset;   // 04
  uint  TriangleOffset; // 08
  uint  VertexCount;    // 12
  uint  TriangleCount;  // 16
  half4 BoundingSphere; // 24 // xyz = center, w = radius
  uint  ConeInfo;       // 28 // xyz = axis, w = cutoff
  uint  ConeApex;       // 32 // xyz = offset
};

struct MeshDraw
{
  uint  FirstTransform;
  uint  TransformCount;
  uint  FirstVertex;
  uint  FirstMeshlet;
  uint  MeshletCount;
  MatID Material;
  uint  Pad0;
  uint  Pad1;
};

struct DrawList
{
  ResID Transforms;
  ResID MeshDraws;
  uint  MeshDrawCount;
  ResID Geometry;
};


#endif
