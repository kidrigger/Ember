#include "DirShadow.hlsli"
#include "Math.hlsli"
#include "Utility.hlsli"

groupshared MeshletPayload pl;

NUM_THREADS( 32, 1, 1 )
void DirShadowAS( uint3 group_id : SV_GroupID, uint3 local_id : SV_GroupThreadID )
{
  ByteAddressBuffer draws         = ResourceDescriptorHeap[g_DrawBatch.DrawBuffer];

  uint              draw_cmd_idx  = group_id.x;
  uint              meshlet_idx   = local_id.x;
  uint              visible_count = 0;

  AmpCommand        cmd = draws.Load<AmpCommand>( g_DrawBatch.CommandsOffset + AmpCommand_size * draw_cmd_idx );

  StructuredBuffer<DirLight> light_data = ResourceDescriptorHeap[g_LightData];

  if ( meshlet_idx < cmd.MeshletCount )
  {
    ByteAddressBuffer ugb          = ResourceDescriptorHeap[g_DrawBatch.GeometryBuffer];

    uint              meshlet_addr = Meshlet_size * ( cmd.FirstMeshlet + meshlet_idx );
    Meshlet           meshlet      = ugb.Load<Meshlet>( meshlet_addr );

    float4x4          model =
        draws.Load<DrawInstance>( g_DrawBatch.InstancesOffset + DrawInstance_size * cmd.InstanceID ).Transform;

    float4 ws_bounds      = TransformBoundingSphere( model, meshlet.BoundingSphere );

    float3 light_dir      = light_data[g_LightIdx].Direction;

    bool   is_inside_prev = false;
    bool   is_view_visible[NUM_CASCADES];
    [unroll] for ( int i = NUM_CASCADES - 1; i >= 0; i-- )
    {
      float4 cascade_cull_info = g_CullParams[i];
      float3 point_to_center   = ( cascade_cull_info.xyz - ws_bounds.xyz );
      float  proj_on_dir       = dot( point_to_center, light_dir );

      // If the proj_on_dir is positive, i.e. center is closer than focus, we use cylinder calculation.
      // If it is farther, then we use spherical - forming a capsule shape with one end at focus, the other at INF.
      float dist = proj_on_dir > 0 ? length( point_to_center - proj_on_dir * light_dir ) : length( point_to_center );

      bool  is_outside    = dist > cascade_cull_info.w + ws_bounds.w;

      is_view_visible[i]  = !is_outside;

      visible_count      += is_view_visible[i] ? 1 : 0;
    }

    if ( visible_count > 0 )
    {
      uint write_idx = WavePrefixSum( visible_count );
      [unroll] for ( int i = 0; i < NUM_CASCADES; i++ )
      {
        if ( is_view_visible[i] )
        {
          pl.MeshletID[write_idx] = meshlet_idx;
          pl.ViewID[write_idx]    = i;
          write_idx++;
        }
      }
    }

    if ( local_id.x == 0 )
    {
      pl.DrawCmdID = draw_cmd_idx;
    }
  }

  DispatchMesh( WaveActiveSum( visible_count ), 1, 1, pl );
}
