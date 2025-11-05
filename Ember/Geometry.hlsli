#ifndef GEOMETRY_HLSLI_
#define GEOMETRY_HLSLI_

#include "Colors.hlsli"
#include "Material.hlsli"
#include "Quantization.hlsli"

struct VertexLite
{
  half4 Position;    // 08
  half2 TexCoord[2]; // 16
};

struct VertexData
{
  uint          Normal;   // 04
  uint          Tangent;  // 08
  PackedColor32 Color;    // 12
  uint          Padding0; // 16

  float4        GetNormal()
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
  uint  VertexDataStart;
  uint  VertexLiteStart;
  uint  FirstMeshlet;
  uint  MeshletCount;
  MatID Material;
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
