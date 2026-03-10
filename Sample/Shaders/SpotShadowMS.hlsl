#include "SpotShadow.hlsli"
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

struct MSVertexOut
{
  float4 ScreenPosition : SV_Position;
};

OUTPUT_TOPOLOGY( "triangle" )
NUM_THREADS( GROUP_SIZE, 1, 1 )
void SpotShadowMS(
    MSIn                         IN,
    in payload SpotShadowPayload amp_payload,
    out vertices MSVertexOut     verts[MAX_VERTS],
    out indices uint3            tris[MAX_TRIANGLES] )
{
  ByteAddressBuffer           draw_buffer  = ResourceDescriptorHeap[g_DrawBatch.DrawBuffer];
  ByteAddressBuffer           ugb          = ResourceDescriptorHeap[g_DrawBatch.GeometryBuffer];

  StructuredBuffer<SpotLight> light_data   = ResourceDescriptorHeap[g_SpotLightBuffer];

  uint                        meshlet_idx  = amp_payload.MeshletID[IN.GroupID.x] + amp_payload.FirstMeshlet;
  uint                        meshlet_addr = Meshlet_size * meshlet_idx;

  Meshlet                     meshlet      = ugb.Load<Meshlet>( meshlet_addr );
  DrawInstance                instance =
      draw_buffer.Load<DrawInstance>( g_DrawBatch.InstancesOffset + DrawInstance_size * amp_payload.InstanceIdx );

  SetMeshOutputCounts( meshlet.VertexCount, meshlet.TriangleCount );

  for ( int i = IN.LocalID.x; i < meshlet.VertexCount; i += GROUP_SIZE )
  {
    uint       index          = ugb.Load( 4 * ( meshlet.VertexOffset + i ) );
    VertexLite vertex         = ugb.Load<VertexLite>( VertexLite_size * ( index + amp_payload.VertexLiteStart ) );

    float4     world_position = mul( instance.Transform, vertex.Position );
    verts[i].ScreenPosition   = mul( light_data[g_LightID].LightSpaceMat, world_position );
  }

  for ( int i = IN.LocalID.x; i < meshlet.TriangleCount; i += GROUP_SIZE )
  {
    tris[i] = LoadBytes3( ugb, meshlet.TriangleOffset + i * 3 );
  }
}
