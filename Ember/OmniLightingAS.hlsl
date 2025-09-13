#include "Bindless.hlsli"
#include "Camera.hlsli"
#include "DebugConfig.hlsli"
#include "LightData.hlsli"
#include "Math.hlsli"
#include "OmniLightCommon.hlsli"
#include "Utility.hlsli"

cbuffer BindlessIndex : register( b1 )
{
  ResID g_Materials;
  ResID g_Camera;
  ResID g_PointLights;
  uint  g_PointLightCount;
  uint  g_ShadowPointLightCount;
  ResID g_DirLights;
  uint  g_DirLightCount;
  uint  g_ShadowDirLightCount;
  ResID g_ConfigID;
}

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

groupshared OmniLightPayload pl;

NUM_THREADS( 32, 1, 1 )
void OmniLightingAS( uint3 dispatch_id : SV_DispatchThreadID )
{
  uint light_idx  = dispatch_id.x;
  bool is_visible = false;

#ifndef STRIP_DEBUG_CONFIG
  ConstantBuffer<DebugConfig> config = ResourceDescriptorHeap[g_ConfigID];
#endif
  StructuredBuffer<PointLight> point_lights = ResourceDescriptorHeap[g_PointLights];

  if ( light_idx >= g_PointLightCount )
  {
    is_visible = false;
  }
  else
  {
    PointLight             pl        = point_lights[NonUniformResourceIndex( light_idx )];
    ConstantBuffer<Camera> camera    = ResourceDescriptorHeap[g_Camera];
    float4                 vs_bounds = TransformBoundingSphere( camera.View, float4( pl.Position, pl.Range ) );

    is_visible                       = !FrustumCull( vs_bounds, camera.CullInfo );
#ifndef STRIP_DEBUG_CONFIG
    is_visible = is_visible && config.IsLitVisMode();
#endif
  }

  if ( is_visible )
  {
    uint out_idx        = WavePrefixCountBits( is_visible );
    pl.LightID[out_idx] = light_idx;
  }

  DispatchMesh( WaveActiveCountBits( is_visible ), 1, 1, pl );
}
