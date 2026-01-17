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
  ByteAddressBuffer draws        = ResourceDescriptorHeap[g_DrawBatch.DrawBuffer];

  uint              draw_cmd_idx = group_id.x;
  uint              meshlet_idx  = local_id.x;
  bool              is_visible   = false;

  AmpCommand        cmd = draws.Load<AmpCommand>( g_DrawBatch.CommandsOffset + AmpCommand_size * draw_cmd_idx );

  StructuredBuffer<SpotLight> spot_lights = ResourceDescriptorHeap[g_SpotLightBuffer];
  SpotLight                   spot_light  = spot_lights[g_LightID];

  if ( meshlet_idx < cmd.MeshletCount )
  {
    ByteAddressBuffer ugb          = ResourceDescriptorHeap[g_DrawBatch.GeometryBuffer];

    uint              meshlet_addr = Meshlet_size * ( cmd.FirstMeshlet + meshlet_idx );
    Meshlet           meshlet      = ugb.Load<Meshlet>( meshlet_addr );

    DrawInstance      instance =
        draws.Load<DrawInstance>( g_DrawBatch.InstancesOffset + DrawInstance_size * cmd.InstanceID );

    float4 ws_bounds = TransformBoundingSphere( instance.Transform, meshlet.BoundingSphere );
    is_visible       = true;
    //! IsCulled(ws_bounds, spot_light.Position, spot_light.Direction, spot_light.ConeOuterCutoff);

    DrawMesh mesh = draws.Load<DrawMesh>( /* Meshoffset + */ DrawMesh_size * instance.MeshID );

    if ( is_visible )
    {
      uint out_idx          = WavePrefixCountBits( is_visible );
      pl.MeshletID[out_idx] = meshlet_idx;
    }

    if ( local_id.x == 0 )
    {
      pl.FirstMeshlet    = cmd.FirstMeshlet;
      pl.VertexLiteStart = mesh.VertexLiteStart;
      pl.InstanceIdx     = cmd.InstanceID;
    }
  }

  DispatchMesh( WaveActiveCountBits( is_visible ), 1, 1, pl );
}
