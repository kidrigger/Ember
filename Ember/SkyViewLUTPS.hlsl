#include "AtmosphereCommon.hlsli"
#include "Bindless.hlsli"
#include "Camera.hlsli"
#include "LightData.hlsli"

cbuffer AtmosphereAndSun : register( b0 )
{
  Atmosphere g_Atmosphere;
  uint       g_SunIndex;
}

cbuffer BindlessIndex : register( b1 )
{
  ResID g_Materials;
  ResID g_Camera;
  ResID g_ConfigID;
  ResID g_PointLights;
  uint  g_ShadowPointLightCount;
  uint  g_PointLightCount;
  ResID g_DirLights;
  uint  g_ShadowDirLightCount;
  uint  g_DirLightCount;
  ResID g_SpotLights;
  uint  g_ShadowSpotLightCount;
  uint  g_SpotLightCount;
}

cbuffer TransmittanceLUT : register( b2 )
{
  ResID g_TransmissionLUT;
}

SamplerState g_LUTSampler : register( s0 );

float3       L( float3 c, float3 v, in DirLight sun )
{
  v        = normalize( v );
  float r  = length( c );
  float mu = dot( c, v ) / r;

  float alen;
  DistanceToSphereFromInsidePolar( alen, kRa, r, mu );

  float     glen;
  bool      intersects_ground = DistanceToSpherePolar( glen, kRg, r, mu );
  float     len               = min( alen, glen );
  int       lim               = g_Atmosphere.ViewSamples;
  float     dt                = len / float( lim );

  float3    acc               = 0.0f;

  Texture2D transmittance_lut = ResourceDescriptorHeap[g_TransmissionLUT];

  if ( !intersects_ground )
  {
    for ( int i = 0; i <= lim; ++i )
    {
      float t = i * dt;
      // Scattering on the path from the camera to the point
      acc += InScattering( c, c + t * v, v, sun, g_Atmosphere, transmittance_lut, g_LUTSampler ) * dt *
             ( i == 0 || i == lim ? 0.5f : 1.0f );
    }
  }
  else
  {
    for ( int i = 0; i <= lim; ++i )
    {
      float t = i * dt;
      // The ray should never hit the ground, so we reverse the points.
      // TODO: This seems wrong, double check.
      acc += InScattering( c + t * v, c, v, sun, g_Atmosphere, transmittance_lut, g_LUTSampler ) * dt *
             ( i == 0 || i == lim ? 0.5f : 1.0f );
    }
  }

  // Ground contribution
  acc += step( glen, 0 ) * TransmittancePointAtoB( c + len * v, c, transmittance_lut, g_LUTSampler ) *
         -normalize( sun.Direction ).y * 0.3f * sun.GetRadiance();

  return acc;
}

float3 GetSkyView( float2 longlat )
{
  ConstantBuffer<Camera>     camera     = ResourceDescriptorHeap[g_Camera];
  StructuredBuffer<DirLight> dir_lights = ResourceDescriptorHeap[g_DirLights];
  DirLight                   sun        = dir_lights[g_SunIndex];

  float3                     dir        = GetSkyViewDirFromLongLat( longlat );
  float3                     x          = float3( 0, camera.Position.y + kRg, 0 );
  return L( x, dir, sun );
}

struct PSIn
{
  float4 Position : SV_POSITION;
  float2 TexCoord : TEXCOORD;
};

float4 SkyViewLUTPS( PSIn IN ) : SV_TARGET
{
  float2 longlat = GetSkyViewLongLatFromUV( IN.TexCoord );
  return float4( GetSkyView( longlat ), 1.0f );
}
