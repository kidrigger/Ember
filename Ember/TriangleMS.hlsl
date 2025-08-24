#include "Triangle.hlsli"
#include "Utility.hlsli"

#define MAX_VERTS 64
#define MAX_TRIANGLES 124

struct MSIn
{
  uint3 group_id : SV_GroupID;
  uint3 local_id : SV_GroupThreadID;
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
void TriangleMS( MSIn IN, out vertices VSOut verts[MAX_VERTS], out indices uint3 tris[MAX_TRIANGLES] )
{
  StructuredBuffer<Meshlet> meshlets          = ResourceDescriptorHeap[g_MeshletBufferIdx];
  StructuredBuffer<uint>    meshlet_vertices  = ResourceDescriptorHeap[g_MeshletVerticesIdx];
  ByteAddressBuffer         meshlet_triangles = ResourceDescriptorHeap[g_MeshletTrianglesIdx];
  ConstantBuffer<Camera>    camera            = ResourceDescriptorHeap[g_Camera];
  StructuredBuffer<Vertex>  vertex_buffer     = ResourceDescriptorHeap[g_VertexBufferIdx];

  Meshlet                   meshlet           = meshlets[NonUniformResourceIndex( IN.group_id.x + g_FirstMeshlet )];

  SetMeshOutputCounts( meshlet.VertexCount, meshlet.TriangleCount );

  if ( IN.local_id.x < meshlet.VertexCount )
  {
    uint   index      = meshlet_vertices[NonUniformResourceIndex( meshlet.VertexOffset + IN.local_id.x )];

    Vertex vertex     = vertex_buffer[NonUniformResourceIndex( index + g_FirstVertex )];

    float4 world_pos  = mul( g_Model, vertex.GetPosition() );
    float4 clip_pos   = mul( camera.View, world_pos );
    float4 screen_pos = mul( camera.Projection, clip_pos );

    float3 normal     = normalize( mul( vertex.GetNormal(), g_InvModel ).xyz );
    float4 tangent    = vertex.GetTangent();
    tangent           = float4( normalize( mul( float4( tangent.xyz, 0.0f ), g_InvModel ).xyz ), tangent.w );

    verts[IN.local_id.x].ScreenPosition = screen_pos;
    verts[IN.local_id.x].Position       = world_pos;
    verts[IN.local_id.x].Normal         = normal;
    verts[IN.local_id.x].LinearDepth    = clip_pos.z;
    verts[IN.local_id.x].Tangent        = tangent;
    verts[IN.local_id.x].Color          = vertex.GetColor();
    verts[IN.local_id.x].TexCoord[0]    = vertex.GetTexCoord( 0 );
    verts[IN.local_id.x].TexCoord[1]    = vertex.GetTexCoord( 1 );
  }

  if ( IN.local_id.x < meshlet.TriangleCount )
  {
    uint offset = meshlet.TriangleOffset + IN.local_id.x * 3;
    // This is wrong.
    uint  buf_offset    = ( offset & ~3 );
    uint  sub_offset    = ( offset & 3 );
    uint2 data          = meshlet_triangles.Load2( buf_offset );
    tris[IN.local_id.x] = GetBytes( data, sub_offset );
  }
}
