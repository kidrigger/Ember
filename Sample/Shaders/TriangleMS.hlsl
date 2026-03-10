#include "Math.hlsli"
#include "Triangle.hlsli"
#include "Utility.hlsli"

#define MAX_VERTS 64
#define MAX_TRIANGLES 124

struct MSIn
{
  uint3 GroupID : SV_GroupID;
  uint3 LocalID : SV_GroupThreadID;
};

OUTPUT_TOPOLOGY( "triangle" )
NUM_THREADS( 32, 1, 1 )
void TriangleMS(
    MSIn                          IN,
    in payload MeshletPayload     amp_payload,
    out vertices MSVertexOut      verts[MAX_VERTS],
    out indices uint3             tris[MAX_TRIANGLES],
    out primitives MSPrimitiveOut materials[MAX_TRIANGLES] )
{
  ByteAddressBuffer draw_buffer  = ResourceDescriptorHeap[g_DrawBatch.DrawBuffer];
  ByteAddressBuffer ugb          = ResourceDescriptorHeap[g_DrawBatch.GeometryBuffer];

  uint              meshlet_idx  = amp_payload.MeshletID[IN.GroupID.x] + amp_payload.FirstMeshlet;
  uint              meshlet_addr = Meshlet_size * meshlet_idx;

  DrawInstance      instance =
      draw_buffer.Load<DrawInstance>( g_DrawBatch.InstancesOffset + DrawInstance_size * amp_payload.InstanceIdx );
  Meshlet  meshlet = ugb.Load<Meshlet>( meshlet_addr );

  DrawMesh mesh    = draw_buffer.Load<DrawMesh>( /* Meshoffset + */ DrawMesh_size * instance.MeshID );

  SetMeshOutputCounts( meshlet.VertexCount, meshlet.TriangleCount );

  for ( int i = IN.LocalID.x; i < meshlet.VertexCount; i += 32 )
  {
    uint       index      = ugb.Load( 4 * ( meshlet.VertexOffset + i ) );

    VertexLite vertex_pos = ugb.Load<VertexLite>( VertexLite_size * ( index + mesh.VertexLiteStart ) );
    VertexData vertex     = ugb.Load<VertexData>( VertexData_size * ( index + mesh.VertexDataStart ) );

    float4     world_pos  = mul( instance.Transform, vertex_pos.Position );
    float4     clip_pos   = mul( g_Camera.View, world_pos );
    float4     screen_pos = mul( g_Camera.Projection, clip_pos );

    float3     normal     = normalize( mul( vertex.GetNormal(), instance.InvTransform ).xyz );
    float4     tangent    = vertex.GetTangent();
    tangent = float4( normalize( mul( float4( tangent.xyz, 0.0f ), instance.InvTransform ).xyz ), tangent.w );

    verts[i].ScreenPosition = screen_pos;
    verts[i].Position       = world_pos;
    verts[i].Normal         = normal;
    verts[i].Tangent        = tangent;
    verts[i].Color          = vertex.GetColor();
    verts[i].TexCoord[0]    = vertex_pos.TexCoord[0];
    verts[i].TexCoord[1]    = vertex_pos.TexCoord[1];
  }

  for ( int i = IN.LocalID.x; i < meshlet.TriangleCount; i += 32 )
  {
    tris[i]               = LoadBytes3( ugb, meshlet.TriangleOffset + i * 3 );

    materials[i].Material = mesh.Material;
#ifndef STRIP_DEBUG_CONFIG
    materials[i].MeshletColor = UnpackColor32( SimpleHash( meshlet_idx ) ).rgb;
#endif
  }
}
