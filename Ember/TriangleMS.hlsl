#include "Triangle.hlsli"
#include "Utility.hlsli"

#define MAX_VERTS 64
#define MAX_TRIANGLES 124

struct MSIn
{
  uint3 GroupID : SV_GroupID;
  uint3 LocalID : SV_GroupThreadID;
};

struct PrimitiveOut
{
  MatID Material : MATERIAL;
};

uint3 GetBytes( uint2 value, uint sub_offset )
{
  return uint3(
      value[sub_offset >> 2] >> ( ( sub_offset % 4 ) * 8 ) & 0xFF,
      value[( sub_offset + 1 ) >> 2] >> ( ( ( sub_offset + 1 ) % 4 ) * 8 ) & 0xFF,
      value[( sub_offset + 2 ) >> 2] >> ( ( ( sub_offset + 2 ) % 4 ) * 8 ) & 0xFF );
}

OUTPUT_TOPOLOGY( "triangle" )
NUM_THREADS( 128, 1, 1 )
void TriangleMS(
    MSIn                        IN,
    in payload MeshletPayload   meshlet_draw,
    out vertices VSOut          verts[MAX_VERTS],
    out indices uint3           tris[MAX_TRIANGLES],
    out primitives PrimitiveOut materials[MAX_TRIANGLES] )
{
  StructuredBuffer<Transform> transforms        = ResourceDescriptorHeap[g_DrawList.Transforms];
  StructuredBuffer<Meshlet>   meshlets          = ResourceDescriptorHeap[meshlet_draw.MeshletBuffer];
  StructuredBuffer<uint>      meshlet_indices   = ResourceDescriptorHeap[meshlet_draw.MeshletIndexBuffer];
  ByteAddressBuffer           meshlet_triangles = ResourceDescriptorHeap[meshlet_draw.MeshletTriangleBuffer];
  StructuredBuffer<Vertex>    vertex_buffer     = ResourceDescriptorHeap[meshlet_draw.VertexBuffer];
  ConstantBuffer<Camera>      camera            = ResourceDescriptorHeap[g_Camera];

  uint                        meshlet_idx       = meshlet_draw.MeshletID[IN.GroupID.x] + meshlet_draw.FirstMeshlet;

  Meshlet                     meshlet           = meshlets[NonUniformResourceIndex( meshlet_idx )];
  Transform                   transform         = transforms[NonUniformResourceIndex( meshlet_draw.Transform )];

  SetMeshOutputCounts( meshlet.VertexCount, meshlet.TriangleCount );

  if ( IN.LocalID.x < meshlet.VertexCount )
  {
    uint   index      = meshlet_indices[NonUniformResourceIndex( meshlet.VertexOffset + IN.LocalID.x )];

    Vertex vertex     = vertex_buffer[NonUniformResourceIndex( index + meshlet_draw.FirstVertex )];

    float4 world_pos  = mul( transform.Model, vertex.GetPosition() );
    float4 clip_pos   = mul( camera.View, world_pos );
    float4 screen_pos = mul( camera.Projection, clip_pos );

    float3 normal     = normalize( mul( vertex.GetNormal(), transform.InvModel ).xyz );
    float4 tangent    = vertex.GetTangent();
    tangent           = float4( normalize( mul( float4( tangent.xyz, 0.0f ), transform.InvModel ).xyz ), tangent.w );

    verts[IN.LocalID.x].ScreenPosition = screen_pos;
    verts[IN.LocalID.x].Position       = world_pos;
    verts[IN.LocalID.x].Normal         = normal;
    verts[IN.LocalID.x].LinearDepth    = clip_pos.z;
    verts[IN.LocalID.x].Tangent        = tangent;
    verts[IN.LocalID.x].Color          = vertex.GetColor();
    verts[IN.LocalID.x].TexCoord[0]    = vertex.GetTexCoord( 0 );
    verts[IN.LocalID.x].TexCoord[1]    = vertex.GetTexCoord( 1 );
  }

  if ( IN.LocalID.x < meshlet.TriangleCount )
  {
    uint offset = meshlet.TriangleOffset + IN.LocalID.x * 3;
    // This is wrong.
    uint  buf_offset                 = ( offset & ~3 );
    uint  sub_offset                 = ( offset & 3 );
    uint2 data                       = meshlet_triangles.Load2( buf_offset );
    tris[IN.LocalID.x]               = GetBytes( data, sub_offset );

    materials[IN.LocalID.x].Material = meshlet_draw.Material;
  }
}
