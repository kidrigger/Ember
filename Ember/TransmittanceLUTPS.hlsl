#include "AtmosphereCommon.hlsli"

cbuffer AtmosphereAndSun : register( b0 )
{
  Atmosphere g_Atmosphere;
  uint       g_SunIndex;
}

cbuffer Unused : register( b1 )
{}

cbuffer Unused2 : register( b2 )
{}

float3 TotalExtinction( in Atmosphere atmosphere, float2 rmu, float len )
{
  float  r                    = rmu.x;
  float  mu                   = rmu.y;
  float  dx                   = len / float( atmosphere.DepthSamples );
  float3 extinction           = 0.0f;

  float3 extinction_coeff_mei = atmosphere.ScatterCoeffMei + atmosphere.AbsorptionCoeffMei;

  // Special case for the first and last sample to use the trapezoidal rule
  // i = 0
  extinction += ( atmosphere.DensityRayleigh( r ) * atmosphere.ScatterCoeffRayleigh +
                  atmosphere.DensityMei( r ) * extinction_coeff_mei +
                  atmosphere.DensityOzone( r ) * atmosphere.AbsorptionCoeffOzone ) *
                dx * 0.5f;

  for ( int i = 1; i < atmosphere.DepthSamples; ++i )
  {
    float d_i   = dx * i;
    float r_i   = sqrt( d_i * d_i + 2.0 * r * mu * d_i + r * r );
    extinction += ( atmosphere.DensityRayleigh( r_i ) * atmosphere.ScatterCoeffRayleigh +
                    atmosphere.DensityMei( r_i ) * extinction_coeff_mei +
                    atmosphere.DensityOzone( r_i ) * atmosphere.AbsorptionCoeffOzone ) *
                  dx;
  }

  // i = atmosphere.DepthSamples
  {
    float r_i   = sqrt( len * len + 2.0 * r * mu * len + r * r );
    extinction += ( atmosphere.DensityRayleigh( r_i ) * atmosphere.ScatterCoeffRayleigh +
                    atmosphere.DensityMei( r_i ) * extinction_coeff_mei +
                    atmosphere.DensityOzone( r_i ) * atmosphere.AbsorptionCoeffOzone ) *
                  dx * 0.5f;
  }

  return extinction;
}

float3 CalculateTransmittance( in Atmosphere atmosphere, float2 rmu )
{
  float len_atm;
  DistanceToSphereFromInsidePolar( len_atm, kRa, rmu.x, rmu.y );

  return exp( -TotalExtinction( atmosphere, rmu, len_atm ) );
}

struct PSIn
{
  float4 Position : SV_POSITION;
  float2 TexCoord : TEXCOORD;
};

float4 TransmittanceLUTPS( PSIn IN ) : SV_TARGET
{
  float2 rmu = GetTransmittanceRMuFromUV( IN.TexCoord );

  return float4( CalculateTransmittance( g_Atmosphere, rmu ), 1.0f );
}
