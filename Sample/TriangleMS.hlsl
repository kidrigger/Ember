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

uint3 GetBytes( uint2 value, uint sub_offset )
{
  return uint3(
      value[sub_offset >> 2] >> ( ( sub_offset % 4 ) * 8 ) & 0xFF,
      value[( sub_offset + 1 ) >> 2] >> ( ( ( sub_offset + 1 ) % 4 ) * 8 ) & 0xFF,
      value[( sub_offset + 2 ) >> 2] >> ( ( ( sub_offset + 2 ) % 4 ) * 8 ) & 0xFF );
}

OUTPUT_TOPOLOGY( "triangle" )
NUM_THREADS( 32, 1, 1 )
void TriangleMS(
    MSIn                          IN,
    in payload MeshletPayload     amp_payload,
    out vertices MSVertexOut      verts[MAX_VERTS],
    out indices uint3             tris[MAX_TRIANGLES],
    out primitives MSPrimitiveOut materials[MAX_TRIANGLES] )
{
  StructuredBuffer<Transform> transforms   = ResourceDescriptorHeap[g_DrawList.Transforms];
  ByteAddressBuffer           ugb          = ResourceDescriptorHeap[g_DrawList.Geometry];
  ConstantBuffer<Camera>      camera       = ResourceDescriptorHeap[g_Camera];

  uint                        meshlet_idx  = amp_payload.MeshletID[IN.GroupID.x] + amp_payload.FirstMeshlet;
  uint                        meshlet_addr = sizeof( Meshlet ) * meshlet_idx;

  Meshlet                     meshlet      = ugb.Load<Meshlet>( meshlet_addr );
  Transform                   transform    = transforms[NonUniformResourceIndex( amp_payload.Transform )];

  SetMeshOutputCounts( meshlet.VertexCount, meshlet.TriangleCount );

  for ( int i = IN.LocalID.x; i < meshlet.VertexCount; i += 32 )
  {
    uint       index      = ugb.Load( 4 * ( meshlet.VertexOffset + i ) );

    VertexLite vertex_pos = ugb.Load<VertexLite>( sizeof( VertexLite ) * ( index + amp_payload.VertexLiteStart ) );
    VertexData vertex     = ugb.Load<VertexData>( sizeof( VertexData ) * ( index + amp_payload.FirstVertex ) );

    float4     world_pos  = mul( transform.Model, vertex_pos.Position );
    float4     clip_pos   = mul( camera.View, world_pos );
    float4     screen_pos = mul( camera.Projection, clip_pos );

    float3     normal     = normalize( mul( vertex.GetNormal(), transform.InvModel ).xyz );
    float4     tangent    = vertex.GetTangent();
    tangent = float4( normalize( mul( float4( tangent.xyz, 0.0f ), transform.InvModel ).xyz ), tangent.w );

    verts[i].ScreenPosition = screen_pos;
    verts[i].Position       = world_pos;
    verts[i].Normal         = normal;
    verts[i].LinearDepth    = clip_pos.z;
    verts[i].Tangent        = tangent;
    verts[i].Color          = vertex.GetColor();
    verts[i].TexCoord[0]    = vertex_pos.TexCoord[0];
    verts[i].TexCoord[1]    = vertex_pos.TexCoord[1];
  }

  for ( int i = IN.LocalID.x; i < meshlet.TriangleCount; i += 32 )
  {
    uint  offset          = meshlet.TriangleOffset + i * 3;
    uint  buf_offset      = ( offset & ~3 );
    uint  sub_offset      = ( offset & 3 );
    uint2 data            = ugb.Load2( buf_offset );
    tris[i]               = GetBytes( data, sub_offset );

    materials[i].Material = amp_payload.Material;
#ifndef STRIP_DEBUG_CONFIG
    materials[i].MeshletColor = UnpackColor32( SimpleHash( meshlet_idx ) ).rgb;
#endif
  }
}
