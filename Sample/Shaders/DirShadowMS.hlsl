#include "DirShadow.hlsli"
#include "Utility.hlsli"

#define MAX_VERTS 64
#define MAX_TRIANGLES 124
#define GROUP_SIZE 32
#define MAX_VERTS_PER_THREAD 2
#define MAX_TRIS_PER_THREAD 4

struct MSIn
{
  uint3 GroupID : SV_GroupID;
  uint3 LocalID : SV_GroupThreadID;
};

OUTPUT_TOPOLOGY( "triangle" )
NUM_THREADS( GROUP_SIZE, 1, 1 )
void DirShadowMS(
    MSIn                          IN,
    in payload MeshletPayload     amp_payload,
    out vertices MSVertexOut      verts[MAX_VERTS],
    out indices uint3             tris[MAX_TRIANGLES],
    out primitives MSPrimitiveOut rt_array_idx[MAX_TRIANGLES] )
{
  ByteAddressBuffer draw_buffer = ResourceDescriptorHeap[g_DrawBatch.DrawBuffer];
  ByteAddressBuffer ugb         = ResourceDescriptorHeap[g_DrawBatch.GeometryBuffer];
  AmpCommand cmd = draw_buffer.Load<AmpCommand>( g_DrawBatch.CommandsOffset + AmpCommand_size * amp_payload.DrawCmdID );

  StructuredBuffer<DirLight> light_data   = ResourceDescriptorHeap[g_Lights.DirLights];

  uint                       meshlet_idx  = amp_payload.MeshletID[IN.GroupID.x] + cmd.FirstMeshlet;
  uint                       view_idx     = amp_payload.ViewID[IN.GroupID.x];

  uint                       meshlet_addr = Meshlet_size * meshlet_idx;

  Meshlet                    meshlet      = ugb.Load<Meshlet>( meshlet_addr );
  DrawInstance               instance =
      draw_buffer.Load<DrawInstance>( g_DrawBatch.InstancesOffset + DrawInstance_size * cmd.InstanceID );

  SetMeshOutputCounts( meshlet.VertexCount, meshlet.TriangleCount );

  DrawMesh mesh = draw_buffer.Load<DrawMesh>( /* Meshoffset + */ DrawMesh_size * instance.MeshID );

  for ( int i = IN.LocalID.x; i < meshlet.VertexCount; i += GROUP_SIZE )
  {
    uint       index            = ugb.Load( 4 * ( meshlet.VertexOffset + i ) );
    VertexLite vertex           = ugb.Load<VertexLite>( VertexLite_size * ( index + mesh.VertexLiteStart ) );

    float4     world_position   = mul( instance.Transform, vertex.Position );
    float4     screen_position  = mul( light_data[g_LightIdx].LightSpaceMat[view_idx], world_position );
    screen_position            /= screen_position.w;

    // Min 0 allows objects behind the near plane to cast shadows.
    // while still allowing max depth precision due to tight bounds for 0-1.
    screen_position.z = max( screen_position.z, 0.0f );

    // Manually calculating the projection
    verts[i].ScreenPosition = screen_position;
    verts[i].WorldPosition  = world_position;
  }

  for ( int i = IN.LocalID.x; i < meshlet.TriangleCount; i += GROUP_SIZE )
  {
    rt_array_idx[i].RTArrayIdx = view_idx;

    tris[i]                    = LoadBytes3( ugb, meshlet.TriangleOffset + i * 3 );
  }
}
