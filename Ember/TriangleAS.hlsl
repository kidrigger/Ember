#include "Triangle.hlsli"
#include "Utility.hlsli"

float PlaneSignedDistance( float4 plane, float3 position )
{
  return dot( plane.xyz, position ) - plane.w;
}

bool IsCulled( float3 center, float radius )
{
  ConstantBuffer<Camera> camera       = ResourceDescriptorHeap[g_Camera];
  float3                 vs_center    = mul( camera.View, float4( center, 1.0f ) ).xyz;

  float4                 near_plane   = float4( 0, 0, -1, camera.CullInfo.z );
  float4                 far_plane    = float4( 0, 0, 1, -camera.CullInfo.w );
  float4                 right_plane  = normalize( float4( -1, 0, -camera.CullInfo.x, 0 ) );
  float4                 left_plane   = normalize( float4( 1, 0, -camera.CullInfo.x, 0 ) );
  float4                 top_plane    = normalize( float4( 0, -1, -camera.CullInfo.y, 0 ) );
  float4                 bottom_plane = normalize( float4( 0, 1, -camera.CullInfo.y, 0 ) );

  if ( PlaneSignedDistance( near_plane, vs_center ) < -radius ) return true;
  if ( PlaneSignedDistance( left_plane, vs_center ) < -radius ) return true;
  if ( PlaneSignedDistance( right_plane, vs_center ) < -radius ) return true;
  if ( PlaneSignedDistance( far_plane, vs_center ) < -radius ) return true;
  if ( PlaneSignedDistance( bottom_plane, vs_center ) < -radius ) return true;
  if ( PlaneSignedDistance( top_plane, vs_center ) < -radius ) return true;

  return false;
}

NUM_THREADS( 32, 1, 1 )
void TriangleAS( uint3 group_id : SV_GroupID, uint3 local_id : SV_GroupThreadID )
{
  uint                       mesh_draw_idx = group_id.x;
  uint                       meshlet_idx   = local_id.x;
  bool                       is_visible    = false;
  MeshletPayload             pl;

  StructuredBuffer<MeshDraw> mesh_draws   = ResourceDescriptorHeap[g_DrawList.MeshDraws];
  MeshDraw                   current_draw = mesh_draws[NonUniformResourceIndex( mesh_draw_idx )];

  if ( meshlet_idx < current_draw.MeshletCount )
  {
    ByteAddressBuffer           meshlet_buffer   = ResourceDescriptorHeap[g_DrawList.Geometry];

    uint                        meshlet_addr     = sizeof( Meshlet ) * ( current_draw.FirstMeshlet + meshlet_idx );
    Meshlet                     meshlet          = meshlet_buffer.Load<Meshlet>( meshlet_addr );

    StructuredBuffer<Transform> transform_buffer = ResourceDescriptorHeap[g_DrawList.Transforms];
    float4x4                    model = transform_buffer[NonUniformResourceIndex( current_draw.FirstTransform )].Model;

    is_visible = !IsCulled( mul( model, float4( meshlet.BoundingSphere.xyz, 1.0f ) ).xyz, meshlet.BoundingSphere.w );

    if ( is_visible )
    {
      uint out_idx          = WavePrefixCountBits( is_visible );
      pl.MeshletID[out_idx] = meshlet_idx;
    }

    pl.Transform    = current_draw.FirstTransform;
    pl.FirstVertex  = current_draw.FirstVertex;
    pl.FirstMeshlet = current_draw.FirstMeshlet;
    pl.Material     = current_draw.Material;
  }

  DispatchMesh( WaveActiveCountBits( is_visible ), 1, 1, pl );
}
