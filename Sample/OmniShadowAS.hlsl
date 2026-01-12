#include "Math.hlsli"
#include "OmniShadow.hlsli"
#include "Utility.hlsli"

#define ROOT2 1.41421356237f

bool IsCulled( float3 ls_center, float radius )
{
  float3 c_o    = ls_center + float3( 0, 0, ROOT2 * radius );
  float2 slopes = c_o.xy / c_o.z;

  if ( abs( slopes.x ) > 1 ) return true;
  if ( abs( slopes.y ) > 1 ) return true;
  if ( ls_center.z > g_FarPlane + radius ) return true;
  if ( ls_center.z < -radius ) return true;

  return false;
}

groupshared MeshletPayload pl;

NUM_THREADS( 32, 1, 1 )
void OmniShadowAS( uint3 group_id : SV_GroupID, uint3 local_id : SV_GroupThreadID )
{
  ByteAddressBuffer draws         = ResourceDescriptorHeap[g_DrawBatch.DrawBuffer];

  uint              draw_cmd_idx  = group_id.x;
  uint              meshlet_idx   = local_id.x;
  uint              visible_count = 0;

  AmpCommand        cmd = draws.Load<AmpCommand>( g_DrawBatch.CommandsOffset + AmpCommand_size * draw_cmd_idx );

  ConstantBuffer<ProjectionTransforms> proj_view = ResourceDescriptorHeap[g_ProjViewID];

  if ( meshlet_idx < cmd.MeshletCount )
  {
    ByteAddressBuffer ugb          = ResourceDescriptorHeap[g_DrawBatch.GeometryBuffer];

    uint              meshlet_addr = Meshlet_size * ( cmd.FirstMeshlet + meshlet_idx );
    Meshlet           meshlet      = ugb.Load<Meshlet>( meshlet_addr );

    float4x4          model =
        draws.Load<DrawInstance>( g_DrawBatch.InstancesOffset + DrawInstance_size * cmd.InstanceID ).Transform;

    float4 bounds = TransformBoundingSphere( model, meshlet.BoundingSphere );

    bool   is_view_visible[6];
    for ( int i = 0; i < 6; i++ )
    {
      // We know this is only translation and orientation. No need for whole transform
      float4 ls_position  = mul( proj_view.Views[i], float4( bounds.xyz - g_LightPosition, 1.0f ) );
      is_view_visible[i]  = !IsCulled( ls_position.xyz, bounds.w );
      visible_count      += is_view_visible[i] ? 1 : 0;
    }

    if ( visible_count > 0 )
    {
      uint write_idx = WavePrefixSum( visible_count );
      for ( int i = 0; i < 6; i++ )
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
