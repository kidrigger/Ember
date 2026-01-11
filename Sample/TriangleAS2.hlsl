#include "DebugConfig.hlsli"
#include "Math.hlsli"
#include "Triangle2.hlsli"
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
void TriangleAS2( uint3 group_id : SV_GroupID, uint3 local_id : SV_GroupThreadID )
{
#ifndef STRIP_DEBUG_CONFIG
  ConstantBuffer<DebugConfig> config = ResourceDescriptorHeap[g_ConfigID];
#endif

  ByteAddressBuffer      draws        = ResourceDescriptorHeap[g_DrawList.DrawBuffer];
  ConstantBuffer<Camera> camera       = ResourceDescriptorHeap[g_Camera];

  uint                   draw_cmd_idx = group_id.x;
  uint                   meshlet_idx  = local_id.x;
  bool                   is_visible   = false;

  AmpCommand cmd = draws.Load<AmpCommand>( g_DrawList.CommandsOffset + AmpCommand_size * draw_cmd_idx );

  if ( meshlet_idx < cmd.MeshletCount )
  {
    ByteAddressBuffer ugb          = ResourceDescriptorHeap[g_DrawList.GeometryBuffer];

    uint              meshlet_addr = Meshlet_size * ( cmd.FirstMeshlet + meshlet_idx );
    Meshlet           meshlet      = ugb.Load<Meshlet>( meshlet_addr );

    float4x4          model =
        draws.Load<DrawInstance>( g_DrawList.InstancesOffset + DrawInstance_size * cmd.InstanceID ).Transform;

    float4 ws_bounds = TransformBoundingSphere( model, meshlet.BoundingSphere );
    float4 vs_bounds = TransformBoundingSphere( camera.View, ws_bounds );

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
      pl.InstanceIdx  = cmd.InstanceID;
      pl.FirstMeshlet = cmd.FirstMeshlet;
    }
  }

  DispatchMesh( WaveActiveCountBits( is_visible ), 1, 1, pl );
}
