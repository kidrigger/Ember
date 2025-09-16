#include "Math.hlsli"
#include "SpotShadow.hlsli"
#include "Utility.hlsli"

#define ROOT2 1.41421356237f

bool IsCulled( float4 bounds, float3 position, float3 direction, float cone_cutoff )
{
  float3 to_light         = position - bounds.xyz;
  float  distance_squared = dot( to_light, to_light );
  float  radius_squared   = bounds.w * bounds.w;

  if ( distance_squared > radius_squared ) return true;

  float3 radius_corrected = to_light - direction * bounds.w / sqrt( 1.0f - cone_cutoff * cone_cutoff );

  return false;
}

groupshared SpotShadowPayload pl;

NUM_THREADS( 32, 1, 1 )
void SpotShadowAS( uint3 group_id : SV_GroupID, uint3 local_id : SV_GroupThreadID )
{
  uint                        mesh_draw_idx = group_id.x;
  uint                        meshlet_idx   = local_id.x;
  bool                        is_visible    = false;

  StructuredBuffer<MeshDraw>  mesh_draws    = ResourceDescriptorHeap[g_DrawList.MeshDraws];
  MeshDraw                    current_draw  = mesh_draws[NonUniformResourceIndex( mesh_draw_idx )];

  StructuredBuffer<SpotLight> spot_lights   = ResourceDescriptorHeap[g_SpotLightBuffer];
  SpotLight                   spot_light    = spot_lights[g_LightID];

  if ( meshlet_idx < current_draw.MeshletCount )
  {
    ByteAddressBuffer           meshlet_buffer   = ResourceDescriptorHeap[g_DrawList.Geometry];

    uint                        meshlet_addr     = sizeof( Meshlet ) * ( current_draw.FirstMeshlet + meshlet_idx );
    Meshlet                     meshlet          = meshlet_buffer.Load<Meshlet>( meshlet_addr );

    StructuredBuffer<Transform> transform_buffer = ResourceDescriptorHeap[g_DrawList.Transforms];
    float4x4                    model = transform_buffer[NonUniformResourceIndex( current_draw.FirstTransform )].Model;

    float4                      ws_bounds = TransformBoundingSphere( model, meshlet.BoundingSphere );
    is_visible                            = true;
    //! IsCulled(ws_bounds, spot_light.Position, spot_light.Direction, spot_light.ConeOuterCutoff);

    if ( is_visible )
    {
      uint out_idx          = WavePrefixCountBits( is_visible );
      pl.MeshletID[out_idx] = meshlet_idx;
    }

    if ( local_id.x == 0 )
    {
      pl.FirstMeshlet   = current_draw.FirstMeshlet;
      pl.FirstVertex    = current_draw.FirstVertex;
      pl.FirstTransform = current_draw.FirstTransform;
    }
  }

  DispatchMesh( WaveActiveCountBits( is_visible ), 1, 1, pl );
}
