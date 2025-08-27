#include "DirShadow.hlsli"
#include "Utility.hlsli"

bool IsCulled( float3 ndc_center, float3 ndc_bounds )
{
  if ( ndc_center.x + ndc_bounds.x < -1.0f ) return true;
  if ( ndc_center.x - ndc_bounds.x > 1.0f ) return true;
  if ( ndc_center.y + ndc_bounds.y < -1.0f ) return true;
  if ( ndc_center.y - ndc_bounds.y > 1.0f ) return true;
  if ( ndc_center.z - ndc_bounds.z > 1.0f ) return true;

  // -- We don't cull against near plane. 'Infinite'.
  // if ( ndc_center.z + ndc_bounds.z < 0.0f ) return true;

  return false;
}

NUM_THREADS( 32, 1, 1 )
void DirShadowAS( uint3 group_id : SV_GroupID, uint3 local_id : SV_GroupThreadID )
{
  uint                       mesh_draw_idx = group_id.x;
  uint                       meshlet_idx   = local_id.x;
  uint                       visible_count = 0;
  MeshletPayload             pl;

  StructuredBuffer<MeshDraw> mesh_draws   = ResourceDescriptorHeap[g_MeshDraws];
  MeshDraw                   current_draw = mesh_draws[NonUniformResourceIndex( mesh_draw_idx )];

  StructuredBuffer<DirLight> light_data   = ResourceDescriptorHeap[g_LightData];

  if ( meshlet_idx < current_draw.MeshletCount )
  {
    StructuredBuffer<Meshlet>   meshlet_buffer   = ResourceDescriptorHeap[current_draw.MeshletBuffer];
    StructuredBuffer<Transform> transform_buffer = ResourceDescriptorHeap[g_Transforms];

    Meshlet                     meshlet          = meshlet_buffer[current_draw.FirstMeshlet + meshlet_idx];
    float4x4                    model            = transform_buffer[current_draw.FirstTransform].Model;
    float4                      ws_center        = mul( model, float4( meshlet.BoundingSphere.xyz, 1.0f ) );

    bool                        is_view_visible[6];
    [unroll] for ( int i = 0; i < 6; i++ )
    {
      float4x4 light_mat   = light_data[g_LightIdx].LightSpaceMat[i];
      float4   ndc_center  = mul( light_mat, ws_center );
      float3   bounds      = abs( mul( light_mat, float4( meshlet.BoundingSphere.www, 1.0f ) ).xyz );

      is_view_visible[i]   = !IsCulled( ndc_center.xyz, bounds );
      visible_count       += is_view_visible[i] ? 1 : 0;
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

    pl.MeshDrawID = mesh_draw_idx;
  }

  DispatchMesh( WaveActiveSum( visible_count ), 1, 1, pl );
}
