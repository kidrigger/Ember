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

uint3 GetBytes( uint2 value, uint sub_offset )
{
  return uint3(
      value[sub_offset >> 2] >> ( ( sub_offset % 4 ) * 8 ) & 0xFF,
      value[( sub_offset + 1 ) >> 2] >> ( ( ( sub_offset + 1 ) % 4 ) * 8 ) & 0xFF,
      value[( sub_offset + 2 ) >> 2] >> ( ( ( sub_offset + 2 ) % 4 ) * 8 ) & 0xFF );
}

OUTPUT_TOPOLOGY( "triangle" )
NUM_THREADS( GROUP_SIZE, 1, 1 )
void SpotShadowMS(
    MSIn                         IN,
    in payload SpotShadowPayload amp_payload,
    out vertices MSVertexOut     verts[MAX_VERTS],
    out indices uint3            tris[MAX_TRIANGLES] )
{
  StructuredBuffer<Transform> transforms        = ResourceDescriptorHeap[g_DrawList.Transforms];

  ByteAddressBuffer           meshlets          = ResourceDescriptorHeap[g_DrawList.Geometry];
  ByteAddressBuffer           meshlet_indices   = ResourceDescriptorHeap[g_DrawList.Geometry];
  ByteAddressBuffer           meshlet_triangles = ResourceDescriptorHeap[g_DrawList.Geometry];
  ByteAddressBuffer           vertex_buffer     = ResourceDescriptorHeap[g_DrawList.Geometry];

  StructuredBuffer<SpotLight> light_data        = ResourceDescriptorHeap[g_SpotLightBuffer];

  uint                        meshlet_idx       = amp_payload.MeshletID[IN.GroupID.x] + amp_payload.FirstMeshlet;

  uint                        meshlet_addr      = sizeof( Meshlet ) * meshlet_idx;

  Meshlet                     meshlet           = meshlets.Load<Meshlet>( meshlet_addr );
  Transform                   transform         = transforms[amp_payload.FirstTransform];

  SetMeshOutputCounts( meshlet.VertexCount, meshlet.TriangleCount );

  for ( int i = IN.LocalID.x; i < meshlet.VertexCount; i += GROUP_SIZE )
  {
    uint   index            = meshlet_indices.Load<uint>( sizeof( uint ) * ( meshlet.VertexOffset + i ) );
    float4 vertex           = vertex_buffer.Load<half4>( sizeof( Vertex ) * ( index + amp_payload.FirstVertex ) );

    float4 world_position   = mul( transform.Model, vertex );
    verts[i].ScreenPosition = mul( light_data[g_LightID].LightSpaceMat, world_position );
  }

  for ( int i = IN.LocalID.x; i < meshlet.TriangleCount; i += GROUP_SIZE )
  {
    uint  offset     = meshlet.TriangleOffset + i * 3;
    uint  buf_offset = ( offset & ~3 );
    uint  sub_offset = ( offset & 3 );
    uint2 data       = meshlet_triangles.Load2( buf_offset );

    // TODO: Increase gap to reduce LGSB stalls
    tris[i] = GetBytes( data, sub_offset );
  }
}
