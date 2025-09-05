#include "DebugConfig.hlsli"
#include "Math.hlsli"
#include "Triangle.hlsli"
#include "Utility.hlsli"

bool FrustumCull( float4 vs_bounds, float4 frust )
{
  float4 near_plane   = float4( 0, 0, -1, frust.z );
  float4 far_plane    = float4( 0, 0, 1, -frust.w );
  float4 right_plane  = normalize( float4( -1, 0, -frust.x, 0 ) );
  float4 left_plane   = normalize( float4( 1, 0, -frust.x, 0 ) );
  float4 top_plane    = normalize( float4( 0, -1, -frust.y, 0 ) );
  float4 bottom_plane = normalize( float4( 0, 1, -frust.y, 0 ) );

  if ( PlaneSignedDistance( near_plane, vs_bounds.xyz ) < -vs_bounds.w ) return true;
  if ( PlaneSignedDistance( left_plane, vs_bounds.xyz ) < -vs_bounds.w ) return true;
  if ( PlaneSignedDistance( right_plane, vs_bounds.xyz ) < -vs_bounds.w ) return true;
  if ( PlaneSignedDistance( far_plane, vs_bounds.xyz ) < -vs_bounds.w ) return true;
  if ( PlaneSignedDistance( bottom_plane, vs_bounds.xyz ) < -vs_bounds.w ) return true;
  if ( PlaneSignedDistance( top_plane, vs_bounds.xyz ) < -vs_bounds.w ) return true;

  return false;
}

groupshared MeshletPayload pl;

NUM_THREADS( 32, 1, 1 )
void TriangleAS( uint3 group_id : SV_GroupID, uint3 local_id : SV_GroupThreadID )
{
#ifndef STRIP_DEBUG_CONFIG
  ConstantBuffer<DebugConfig> config = ResourceDescriptorHeap[g_ConfigID];
#endif

  uint                       mesh_draw_idx = group_id.x;
  uint                       meshlet_idx   = local_id.x;
  bool                       is_visible    = false;

  StructuredBuffer<MeshDraw> mesh_draws    = ResourceDescriptorHeap[g_DrawList.MeshDraws];
  MeshDraw                   current_draw  = mesh_draws[NonUniformResourceIndex( mesh_draw_idx )];

  ConstantBuffer<Camera>     camera        = ResourceDescriptorHeap[g_Camera];

  if ( meshlet_idx < current_draw.MeshletCount )
  {
    ByteAddressBuffer           meshlet_buffer   = ResourceDescriptorHeap[g_DrawList.Geometry];

    uint                        meshlet_addr     = sizeof( Meshlet ) * ( current_draw.FirstMeshlet + meshlet_idx );
    Meshlet                     meshlet          = meshlet_buffer.Load<Meshlet>( meshlet_addr );

    StructuredBuffer<Transform> transform_buffer = ResourceDescriptorHeap[g_DrawList.Transforms];
    float4x4                    model = transform_buffer[NonUniformResourceIndex( current_draw.FirstTransform )].Model;

    float4                      ws_bounds = TransformBoundingSphere( model, meshlet.BoundingSphere );
    float4                      vs_bounds = TransformBoundingSphere( camera.View, ws_bounds );

#ifndef STRIP_DEBUG_CONFIG
    if ( !config.DisableMeshletFrustumCulling )
    {
      is_visible = !FrustumCull( vs_bounds, camera.CullInfo );
    }
    else
    {
      is_visible = true;
    }
#else
    is_visible = !FrustumCull( vs_bounds, camera.CullInfo );
#endif

    if ( is_visible )
    {
      uint out_idx          = WavePrefixCountBits( is_visible );
      pl.MeshletID[out_idx] = meshlet_idx;
    }

    if ( local_id.x == 0 )
    {
      pl.Transform    = current_draw.FirstTransform;
      pl.FirstVertex  = current_draw.FirstVertex;
      pl.FirstMeshlet = current_draw.FirstMeshlet;
      pl.Material     = current_draw.Material;
    }
  }

  DispatchMesh( WaveActiveCountBits( is_visible ), 1, 1, pl );
}
