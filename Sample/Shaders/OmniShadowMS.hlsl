#include "OmniShadow.hlsli"
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
void OmniShadowMS(
    MSIn                          IN,
    in payload MeshletPayload     amp_payload,
    out vertices MSVertexOut      verts[MAX_VERTS],
    out indices uint3             tris[MAX_TRIANGLES],
    out primitives MSPrimitiveOut rt_array_idx[MAX_TRIANGLES] )
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

  float f = g_FarPlane;
  float n = kNearPlane;

  for ( int i = IN.LocalID.x; i < meshlet.VertexCount; i += 32 )
  {
    uint       index          = ugb.Load( 4 * ( meshlet.VertexOffset + i ) );

    VertexLite vertex         = ugb.Load<VertexLite>( VertexLite_size * ( index + mesh.VertexLiteStart ) );

    float4     world_position = mul( instance.Transform, vertex.Position );

    float4     pos = float4( MulQuatVec( kViewOrientations[view_idx], world_position.xyz - g_LightPosition ), 1.0f );

    // Manually calculating the projection
    verts[i].ScreenPosition = float4( pos.x, pos.y, pos.z * f / ( f - n ) - pos.w * n * f / ( f - n ), pos.z );
    verts[i].WorldPosition  = world_position;
  }

  for ( int i = IN.LocalID.x; i < meshlet.TriangleCount; i += 32 )
  {
    tris[i]                    = LoadBytes3( ugb, meshlet.TriangleOffset + i * 3 );

    rt_array_idx[i].RTArrayIdx = view_idx;
  }
}
