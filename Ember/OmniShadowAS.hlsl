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
  uint                                 mesh_draw_idx = group_id.x;
  uint                                 meshlet_idx   = local_id.x;
  uint                                 visible_count = 0;

  StructuredBuffer<MeshDraw>           mesh_draws    = ResourceDescriptorHeap[g_DrawList.MeshDraws];
  MeshDraw                             current_draw  = mesh_draws[NonUniformResourceIndex( mesh_draw_idx )];

  ConstantBuffer<ProjectionTransforms> proj_view     = ResourceDescriptorHeap[g_ProjViewID];

  if ( meshlet_idx < current_draw.MeshletCount )
  {
    ByteAddressBuffer           meshlet_buffer   = ResourceDescriptorHeap[g_DrawList.Geometry];
    uint                        meshlet_addr     = sizeof( Meshlet ) * ( current_draw.FirstMeshlet + meshlet_idx );
    Meshlet                     meshlet          = meshlet_buffer.Load<Meshlet>( meshlet_addr );

    StructuredBuffer<Transform> transform_buffer = ResourceDescriptorHeap[g_DrawList.Transforms];
    float4x4                    model  = transform_buffer[NonUniformResourceIndex( current_draw.FirstTransform )].Model;
    float4                      bounds = TransformBoundingSphere( model, meshlet.BoundingSphere );

    bool                        is_view_visible[6];
    [unroll] for ( int i = 0; i < 6; i++ )
    {
      // We know this is only translation and orientation. No need for whole transform
      float4 ls_position  = mul( proj_view.Views[i], float4( bounds.xyz - g_LightPosition, 1.0f ) );
      is_view_visible[i]  = !IsCulled( ls_position.xyz, bounds.w );
      visible_count      += is_view_visible[i] ? 1 : 0;
    }

    if ( visible_count > 0 )
    {
      uint write_idx = WavePrefixSum( visible_count );
      [unroll] for ( int i = 0; i < 6; i++ )
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
      pl.MeshDrawID = mesh_draw_idx;
    }
  }

  DispatchMesh( WaveActiveSum( visible_count ), 1, 1, pl );
}
