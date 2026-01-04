#include "DepthPrePass.hlsli"
#include "Math.hlsli"
#include "Utility.hlsli"

#define MAX_VERTS 64
#define MAX_TRIANGLES 124

struct MSIn
{
  uint3 GroupID : SV_GroupID;
  uint3 LocalID : SV_GroupThreadID;
};

uint3 GetBytes( uint2 value, uint sub_offset )
{
  return uint3(
      value[sub_offset >> 2] >> ( ( sub_offset % 4 ) * 8 ) & 0xFF,
      value[( sub_offset + 1 ) >> 2] >> ( ( ( sub_offset + 1 ) % 4 ) * 8 ) & 0xFF,
      value[( sub_offset + 2 ) >> 2] >> ( ( ( sub_offset + 2 ) % 4 ) * 8 ) & 0xFF );
}

OUTPUT_TOPOLOGY( "triangle" )
NUM_THREADS( 32, 1, 1 )
void DepthPrePassMS(
    MSIn                          IN,
    in payload MeshletPayload     meshlet_draw,
    out vertices MSVertexOut      verts[MAX_VERTS],
    out indices uint3             tris[MAX_TRIANGLES],
    out primitives MSPrimitiveOut prims[MAX_TRIANGLES] )
{
  StructuredBuffer<Transform> transforms        = ResourceDescriptorHeap[g_DrawList.Transforms];
  ByteAddressBuffer           meshlets          = ResourceDescriptorHeap[g_DrawList.Geometry];
  ByteAddressBuffer           meshlet_indices   = ResourceDescriptorHeap[g_DrawList.Geometry];
  ByteAddressBuffer           meshlet_triangles = ResourceDescriptorHeap[g_DrawList.Geometry];
  ByteAddressBuffer           vertex_buffer     = ResourceDescriptorHeap[g_DrawList.Geometry];
  ConstantBuffer<Camera>      camera            = ResourceDescriptorHeap[g_Camera];

  uint                        meshlet_idx       = meshlet_draw.MeshletID[IN.GroupID.x] + meshlet_draw.FirstMeshlet;
  uint                        meshlet_addr      = sizeof( Meshlet ) * meshlet_idx;

  Meshlet                     meshlet           = meshlets.Load<Meshlet>( meshlet_addr );
  Transform                   transform         = transforms[NonUniformResourceIndex( meshlet_draw.Transform )];

  SetMeshOutputCounts( meshlet.VertexCount, meshlet.TriangleCount );

  for ( int i = IN.LocalID.x; i < meshlet.VertexCount; i += 32 )
  {
    uint       index  = meshlet_indices.Load( sizeof( uint ) * ( meshlet.VertexOffset + i ) );

    VertexLite vertex = vertex_buffer.Load<VertexLite>( sizeof( VertexLite ) * ( index + meshlet_draw.FirstVertex ) );

    float4     world_pos    = mul( transform.Model, vertex.Position );
    float4     clip_pos     = mul( camera.View, world_pos );
    float4     screen_pos   = mul( camera.Projection, clip_pos );

    verts[i].ScreenPosition = screen_pos;
    verts[i].TexCoord0      = vertex.TexCoord[0];
  }

  for ( int i = IN.LocalID.x; i < meshlet.TriangleCount; i += 32 )
  {
    uint  offset      = meshlet.TriangleOffset + i * 3;
    uint  buf_offset  = ( offset & ~3 );
    uint  sub_offset  = ( offset & 3 );
    uint2 data        = meshlet_triangles.Load2( buf_offset );
    tris[i]           = GetBytes( data, sub_offset );

    prims[i].Material = meshlet_draw.Material;
  }
}
