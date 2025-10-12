#include "AtmosphereCommon.hlsli"
#include "Bindless.hlsli"
#include "Camera.hlsli"

cbuffer AtmosphereAndSun : register( b0 )
{
  Atmosphere g_Atmosphere;
  SunData    g_Sun;
}

cbuffer TransmittanceLUT : register( b1 )
{
  ResID g_TransmissionLUT;
  ResID g_Camera;
}

SamplerState lut_sampler : register( s0 );

// Transmittance
float3 TransmittancePointAtoB( float3 a, float3 b )
{
  Texture2D<float4> transmittance_lut = ResourceDescriptorHeap[g_TransmissionLUT];

  float             len               = distance( a, b );
  if ( len == 0.0f ) return 1.0f;
  float3 v     = ( b - a ) / len;
  float2 rmu_x = GetRMu( a, v );
  float2 rmu_y = GetRMu( b, v );
  float2 xuv   = GetTransmittanceUVFromRMu( rmu_x );
  float2 yuv   = GetTransmittanceUVFromRMu( rmu_y );
  return saturate(
      transmittance_lut.SampleLevel( lut_sampler, xuv, 0 ).rgb /
      transmittance_lut.SampleLevel( lut_sampler, yuv, 0 ).rgb );
}

// Direct visibility (no ground)
float Vis( float2 rmu )
{
  float dg;
  return DistanceToSpherePolar( dg, kRg, rmu.x, rmu.y ) ? 0.0f : 1.0f;
}

float3 ShadowTerm( float3 x, float3 v )
{
  float2 rmu   = GetRMu( x, v );
  float  t_atm = DistanceToAtmosphere( rmu );
  return Vis( rmu ) * TransmittancePointAtoB( x, x + t_atm * v );
}

// In-scattering from the sun
// c: camera position
// x: point in the atmosphere
// v: view direction
float3 InScattering( float3 c, float3 x, float3 v )
{
  float3 li              = -normalize( g_Sun.Direction );
  float  r               = GetR( x );
  float  nu              = dot( v, li );
  float3 rayleigh_factor = Pr( nu ) * g_Atmosphere.ScatterCoeffRayleigh * g_Atmosphere.DensityRayleigh( r );
  float  mei_factor      = g_Atmosphere.Pm( nu ) * g_Atmosphere.ScatterCoeffMei * g_Atmosphere.DensityMei( r );
  return TransmittancePointAtoB( c, x ) * ShadowTerm( x, li ) * ( rayleigh_factor + mei_factor ) * g_Sun.GetRadiance();
}

float3 L( float3 c, float3 v )
{
  v        = normalize( v );
  float r  = length( c );
  float mu = dot( c, v ) / r;

  float alen;
  DistanceToSphereFromInsidePolar( alen, kRa, r, mu );

  float  glen;
  bool   intersects_ground = DistanceToSpherePolar( glen, kRg, r, mu );
  float  len               = min( alen, glen );
  int    lim               = g_Atmosphere.ViewSamples;
  float  dt                = len / float( lim );

  float3 acc               = 0.0f;

  if ( !intersects_ground )
  {
    for ( int i = 0; i <= lim; ++i )
    {
      float t = i * dt;
      // Scattering on the path from the camera to the point
      acc += InScattering( c, c + t * v, v ) * dt * ( i == 0 || i == lim ? 0.5f : 1.0f );
    }
  }
  else
  {
    for ( int i = 0; i <= lim; ++i )
    {
      float t = i * dt;
      // The ray should never hit the ground, so we reverse the points.
      // TODO: This seems wrong, double check.
      acc += InScattering( c + t * v, c, v ) * dt * ( i == 0 || i == lim ? 0.5f : 1.0f );
    }
  }

  acc += step( glen, 0 ) * TransmittancePointAtoB( c + len * v, c ) * -normalize( g_Sun.Direction ).y * 0.3f *
         g_Sun.GetRadiance();

  return acc;
}

float3 GetSkyView( float2 longlat )
{
  ConstantBuffer<Camera> camera = ResourceDescriptorHeap[g_Camera];

  float3                 dir    = GetSkyViewDirFromLongLat( longlat );
  float3                 x      = float3( 0, camera.Position.y + kRg, 0 );
  return L( x, dir );
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
