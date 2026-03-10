#include "ReflectionProbe.hlsli"
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
void ReflectionProbeMS(
    MSIn                          IN,
    in payload MeshletPayload     amp_payload,
    out vertices MSVertexOut      verts[MAX_VERTS],
    out indices uint3             tris[MAX_TRIANGLES],
    out primitives MSPrimitiveOut prims[MAX_TRIANGLES] )
{
  ByteAddressBuffer draw_buffer = ResourceDescriptorHeap[g_DrawBatch.DrawBuffer];
  ByteAddressBuffer ugb         = ResourceDescriptorHeap[g_DrawBatch.GeometryBuffer];
  AmpCommand cmd = draw_buffer.Load<AmpCommand>( g_DrawBatch.CommandsOffset + AmpCommand_size * amp_payload.DrawCmdID );

  uint       meshlet_idx  = amp_payload.MeshletID[IN.GroupID.x] + cmd.FirstMeshlet;
  uint       meshlet_addr = Meshlet_size * meshlet_idx;
  uint       view_idx     = amp_payload.ViewID[IN.GroupID.x];

  Meshlet    meshlet      = ugb.Load<Meshlet>( meshlet_addr );
  DrawInstance instance =
      draw_buffer.Load<DrawInstance>( g_DrawBatch.InstancesOffset + DrawInstance_size * cmd.InstanceID );

  DrawMesh mesh = draw_buffer.Load<DrawMesh>( /* Meshoffset + */ DrawMesh_size * instance.MeshID );

  SetMeshOutputCounts( meshlet.VertexCount, meshlet.TriangleCount );

  float f = g_ProbeInfo.w;
  float n = kNearPlane;

  for ( int i = IN.LocalID.x; i < meshlet.VertexCount; i += 32 )
  {
    uint       index       = ugb.Load( 4 * ( meshlet.VertexOffset + i ) );

    VertexLite vertex_pos  = ugb.Load<VertexLite>( VertexLite_size * ( index + mesh.VertexLiteStart ) );
    VertexData vertex_data = ugb.Load<VertexData>( VertexData_size * ( index + mesh.VertexDataStart ) );

    float4     world_pos   = mul( instance.Transform, vertex_pos.Position );

    float4     clip        = float4( MulQuatVec( kViewOrientations[view_idx], world_pos.xyz - g_ProbeInfo.xyz ), 1.0f );
    float3     normal      = normalize( mul( vertex_data.GetNormal(), instance.InvTransform ).xyz );
    float4     tangent     = vertex_data.GetTangent();
    tangent = float4( normalize( mul( float4( tangent.xyz, 0.0f ), instance.InvTransform ).xyz ), tangent.w );

    // Manually calculating the projection
    verts[i].ScreenPosition = float4( clip.x, clip.y, clip.z * f / ( f - n ) - clip.w * n * f / ( f - n ), clip.z );
    verts[i].WorldPosition  = world_pos;
    verts[i].Normal         = normal;
    verts[i].Tangent        = tangent;
    verts[i].Color          = vertex_data.GetColor();
    verts[i].TexCoord[0]    = vertex_pos.TexCoord[0];
    verts[i].TexCoord[1]    = vertex_pos.TexCoord[1];
  }

  for ( int i = IN.LocalID.x; i < meshlet.TriangleCount; i += 32 )
  {
    tris[i]             = LoadBytes3( ugb, meshlet.TriangleOffset + i * 3 );

    prims[i].Material   = mesh.Material;
    prims[i].RTArrayIdx = view_idx;
  }
}
