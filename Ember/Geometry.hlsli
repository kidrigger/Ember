#ifndef GEOMETRY_HLSLI_
#define GEOMETRY_HLSLI_

struct Transform
{
  float4x4 Model;
  float4x4 InvModel;
};

struct Meshlet
{
  uint VertexOffset;
  uint TriangleOffset;
  uint VertexCount;
  uint TriangleCount;
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
  MatID Material;
  uint  Pad0;
  uint  Pad1;
};

struct MeshletPayload
{
  uint  Transform;
  uint  FirstVertex;
  uint  FirstMeshlet;
  ResID MeshletBuffer;
  ResID MeshletTriangleBuffer;
  ResID MeshletIndexBuffer;
  ResID VertexBuffer;
  MatID Material;
};

struct DrawList
{
  ResID Transforms;
  ResID MeshDraws;
  uint  MeshDrawCount;
};


#endif
