#include "OmniShadow.hlsli"
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
void OmniShadowMS(
    MSIn                          IN,
    in payload MeshletPayload     amp_payload,
    out vertices MSVertexOut      verts[MAX_VERTS],
    out indices uint3             tris[MAX_TRIANGLES],
    out primitives MSPrimitiveOut rt_array_idx[MAX_TRIANGLES] )
{
  StructuredBuffer<Transform>          transforms        = ResourceDescriptorHeap[g_DrawList.Transforms];
  StructuredBuffer<MeshDraw>           mesh_draw_buf     = ResourceDescriptorHeap[g_DrawList.MeshDraws];
  MeshDraw                             mesh_draw         = mesh_draw_buf[amp_payload.MeshDrawID];

  ByteAddressBuffer                    meshlets          = ResourceDescriptorHeap[g_DrawList.Geometry];
  ByteAddressBuffer                    meshlet_indices   = ResourceDescriptorHeap[g_DrawList.Geometry];
  ByteAddressBuffer                    meshlet_triangles = ResourceDescriptorHeap[g_DrawList.Geometry];
  ByteAddressBuffer                    vertex_buffer     = ResourceDescriptorHeap[g_DrawList.Geometry];
  ConstantBuffer<ProjectionTransforms> proj_view         = ResourceDescriptorHeap[g_ProjViewID];

  uint                                 meshlet_idx       = amp_payload.MeshletID[IN.GroupID.x] + mesh_draw.FirstMeshlet;
  uint                                 view_idx          = amp_payload.ViewID[IN.GroupID.x];

  Meshlet                              meshlet           = meshlets.Load<Meshlet>( sizeof( Meshlet ) * meshlet_idx );
  Transform                            transform         = transforms[mesh_draw.FirstTransform];

  SetMeshOutputCounts( meshlet.VertexCount, meshlet.TriangleCount );

  float f = g_FarPlane;
  float n = kNearPlane;

  for ( int i = IN.LocalID.x; i < meshlet.VertexCount; i += 32 )
  {
    uint       index  = meshlet_indices.Load<uint>( sizeof( uint ) * ( meshlet.VertexOffset + i ) );

    VertexLite vertex = vertex_buffer.Load<VertexLite>( sizeof( VertexLite ) * ( index + mesh_draw.VertexLiteStart ) );

    float4     world_position = mul( transform.Model, vertex.Position );

    float4     pos            = mul( proj_view.Views[view_idx], float4( world_position.xyz - g_LightPosition, 1.0f ) );

    // Manually calculating the projection
    verts[i].ScreenPosition = float4( pos.x, pos.y, pos.z * f / ( f - n ) - pos.w * n * f / ( f - n ), pos.z );
    verts[i].WorldPosition  = world_position;
  }

  for ( int i = IN.LocalID.x; i < meshlet.TriangleCount; i += 32 )
  {
    uint  offset               = meshlet.TriangleOffset + i * 3;
    uint  buf_offset           = ( offset & ~3 );
    uint  sub_offset           = ( offset & 3 );
    uint2 data                 = meshlet_triangles.Load2( buf_offset );
    tris[i]                    = GetBytes( data, sub_offset );

    rt_array_idx[i].RTArrayIdx = view_idx;
  }
}
