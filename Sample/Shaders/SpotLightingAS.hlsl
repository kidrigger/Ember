#include "Bindless.hlsli"
#include "Camera.hlsli"
#include "DebugConfig.hlsli"
#include "LightData.hlsli"
#include "Math.hlsli"
#include "SpotLightCommon.hlsli"
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

groupshared SpotLightPayload pl;

NUM_THREADS( 32, 1, 1 )
void SpotLightingAS( uint3 dispatch_id : SV_DispatchThreadID )
{
  uint                        light_idx   = dispatch_id.x;
  bool                        is_visible  = false;

  StructuredBuffer<SpotLight> spot_lights = ResourceDescriptorHeap[g_Lights.SpotLights];

  if ( light_idx >= g_Lights.SpotLightCount )
  {
    is_visible = false;
  }
  else
  {
    SpotLight sl           = spot_lights[NonUniformResourceIndex( light_idx )];

    float     bound_radius = min( sl.Range / 2 * sl.ConeOuterCutoff, sl.Range / 2 );
    float3    bound_center = sl.Position + normalize( sl.Direction ) * bound_radius;

    float4    vs_bounds    = TransformBoundingSphere( g_Camera.View, float4( bound_center, bound_radius ) );

    is_visible             = !FrustumCull( vs_bounds, g_Camera.CullInfo );
#ifndef STRIP_DEBUG_CONFIG
    is_visible = is_visible && g_Debug.IsLitVisMode();
#endif
  }

  if ( is_visible )
  {
    uint out_idx        = WavePrefixCountBits( is_visible );
    pl.LightID[out_idx] = light_idx;
  }

  DispatchMesh( WaveActiveCountBits( is_visible ), 1, 1, pl );
}
