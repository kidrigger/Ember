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

#define VertexLite_size 16

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

#define VertexData_size 16

struct Transform
{
  float4x4 Model;    // 64  64
  float4x4 InvModel; // 64 128
};

#define Transform_size 128

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

#define Meshlet_size 32

struct DrawBatch
{
  ResID GeometryBuffer;  // 04 04
  ResID MaterialBuffer;  // 04 08
  ResID TopLevelAS;      // 04 12
  ResID DrawBuffer;      // 04 16
  uint  InstancesOffset; // 04 20
  uint  CommandsOffset;  // 04 24
  uint  CommandsCount;   // 04 28
};

#define DrawBatch_size 28

// Contains all the information for a single 'mesh'
// TODO: Let this be resident in the VRAM
struct DrawMesh
{
  uint  VertexDataStart; // 04 04
  uint  VertexLiteStart; // 04 08
  MatID Material;        // 04 12
  uint  FirstMeshlet;    // 04 16
  uint  IndexStart;      // 04 20
};

#define DrawMesh_size 20

// What transform, which mesh
// Should be updated every frame. (For dynamic)
struct DrawInstance
{
  float4x4 Transform;    // 64  64
  float4x4 InvTransform; // 64 128
  uint     MeshID;       // 04 132 // TODO: Tuck this into the matrices.
};

#define DrawInstance_size 132

/*
 * Which instance to pick up (for the amplification shader)
 * Update every frame
 *
 * TODO: Split into commands with exactly 32 meshlets.
 * Bucket the rest into a special command set.
 */
struct AmpCommand
{
  uint InstanceID;   // Which instance (index DrawInstance)
  uint FirstMeshlet; // Which meshlet of this instance. (Mesh.FirstMeshlet + FirstMeshlet in the geometry)
  uint MeshletCount; // How many meshlets to draw.
  uint Pad0;
};

#define AmpCommand_size 16

#endif
