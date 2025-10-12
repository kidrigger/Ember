#ifndef _FUNCTIONS_HLSLI
#define _FUNCTIONS_HLSLI

#include "Colors.hlsli"
#include "Math.hlsli"
#include "Utility.hlsli"

static const float3 kUp      = float3( 0, 1, 0 );
static const float3 kRight   = float3( 1, 0, 0 );
static const float3 kForward = float3( 0, 0, 1 );

static const float  kRg      = 6360000.0f;
static const float  kRa      = 6460000.0f;

struct Atmosphere
{
  /* rayleigh */
  float3 ScatterCoeffRayleigh;  // 12
  float  DensityFactorRayleigh; // 16

  /* ozone */
  float3 AbsorptionCoeffOzone; // 28
  float  OzoneHeight;          // 32
  float  OzoneWidth;           // 36

  /* mei */
  float ScatterCoeffMei;    // 40
  float AbsorptionCoeffMei; // 44
  float DensityFactorMei;   // 48

  float AsymmetryMei;       // 52

  /* sampling */
  int   DepthSamples; // 56
  int   ViewSamples;  // 60

  int   Pad0;         // 64

  float DensityRayleigh( float r )
  {
    float h = r - kRg;
    return exp( -h / DensityFactorRayleigh );
  }

  float DensityMei( float r )
  {
    float h = r - kRg;
    return exp( -h / DensityFactorMei );
  }

  float DensityOzone( float r )
  {
    float h = r - kRg;
    return max( 0, 1 - abs( h - OzoneHeight ) * 2.0f / OzoneWidth );
  }

  float Pm( float nu )
  {
    float k      = 3.0f * kPiInv / 8.0f;
    float g      = AsymmetryMei;
    float g2     = g * g;
    float factor = 1 + g2 - 2.0f * g * nu;
    return ( 1 - g2 ) * ( 1 + nu * nu ) / ( ( 2 + g2 ) * pow( max( 0.0000001f, factor ), 1.5f ) );
  }
};

struct SunData
{
  float3        Direction;
  PackedColor32 Color;
  float         Intensity;

  float3        GetRadiance()
  {
    return UnpackColor32( Color ).rgb * Intensity;
  }
};

// Impl

static const int kTransmittanceTextureWidth  = 64;
static const int kTransmittanceTextureHeight = 256;

float            GetR( float3 x )
{
  return length( x );
}

float GetMu( float3 x, float3 v )
{
  return dot( x, v ) / length( x );
}

float2 GetRMu( float3 x, float3 v )
{
  return float2( GetR( x ), GetMu( x, v ) );
}

bool IsInsideAtmosphere( float r )
{
  return r <= kRa;
}

bool IsInsideGround( float r )
{
  return r <= kRg;
}

float DistanceToAtmosphereExt( float2 rmu )
{
  float dist;
  if ( DistanceToSpherePolar( dist, kRa, rmu.x, rmu.y ) ) return dist;
  return kInf;
}

float DistanceToAtmosphereExt( float3 x, float3 v )
{
  return DistanceToAtmosphereExt( GetRMu( x, v ) );
}

float DistanceToAtmosphere( float2 rmu )
{
  float b     = rmu.x * rmu.y;
  float c     = rmu.x * rmu.x - kRa * kRa;
  float delta = sqrt( b * b - c );
  return -b + delta;
}

float DistanceToAtmosphere( float3 x, float3 v )
{
  return DistanceToAtmosphere( GetRMu( x, v ) );
}

float DistanceToGround( float2 rmu )
{
  float dist;
  if ( DistanceToSpherePolar( dist, kRg, rmu.x, rmu.y ) ) return dist;
  return kInf;
}

float DistanceToGround( float3 x, float3 v )
{
  return DistanceToGround( GetRMu( x, v ) );
}

float Pr( float nu )
{
  float k = 3.0f * kPiInv / 16.0f;
  return k * ( 1 + nu * nu );
}

// Mapping each LUT sample to the center of its texel.
float GetTexCoordFromUnitRange( float x, int texture_size )
{
  return 0.5 / float( texture_size ) + x * ( 1.0 - 1.0 / float( texture_size ) );
}

float GetUnitRangeFromTexCoord( float u, int texture_size )
{
  return ( u - 0.5 / float( texture_size ) ) / ( 1.0 - 1.0 / float( texture_size ) );
}

float2 GetTransmittanceRMuFromUV( float2 uv )
{
  float x_r  = GetUnitRangeFromTexCoord( uv.x, kTransmittanceTextureWidth );
  float x_mu = GetUnitRangeFromTexCoord( uv.y, kTransmittanceTextureHeight );
  // Distance to top atmosphere boundary for a horizontal ray at ground level.
  float h = sqrt( kRa * kRa - kRg * kRg );
  // Distance to the horizon, from which we can compute r:
  float rho = h * x_r;
  float r   = sqrt( rho * rho + kRg * kRg );
  // Distance to the top atmosphere boundary for the ray (r,mu), and its minimum
  // and maximum values over all mu - obtained for (r,1) and (r,mu_horizon) -
  // from which we can recover mu:
  float d_min = kRa - r;
  float d_max = rho + h;
  float d     = d_min + x_mu * ( d_max - d_min );
  float mu    = d == 0.0 ? float( 1.0 ) : ( h * h - rho * rho - d * d ) / ( 2.0 * r * d );
  mu          = clamp( mu, -1.0f, 1.0f );

  return float2( r, mu );
}

float2 GetTransmittanceUVFromRMu( float2 rmu )
{
  // Distance to top atmosphere boundary for a horizontal ray at ground level.
  const float h = sqrt( kRa * kRa - kRg * kRg );
  // Distance to the horizon.
  const float rho   = sqrt( max( 0, rmu.x * rmu.x - kRg * kRg ) );
  const float d     = DistanceToAtmosphere( rmu );
  const float d_min = kRa - rmu.x;
  const float d_max = rho + h;
  const float x_mu  = ( d - d_min ) / ( d_max - d_min );
  const float x_r   = rho / h;
  return float2(
      GetTexCoordFromUnitRange( x_r, kTransmittanceTextureWidth ),
      GetTexCoordFromUnitRange( x_mu, kTransmittanceTextureHeight ) );
}

float2 GetSkyViewLongLatFromUV( float2 uv )
{

  float       longitude = kTau * uv.x;
  const float nv        = 2.0f * uv.y - 1.0f;
  float       latitude  = sign( nv ) * nv * nv * 0.5f * kPi;

  return float2( longitude, latitude );
}

float2 GetSkyViewLongLatFromDir( float3 dir )
{
  float2 longlat;
  longlat.y             = asin( dir.y );
  const float2 norm_dir = normalize( dir.xz );
  longlat.x             = ( norm_dir.y < 0 ? 2 * kPi - acos( norm_dir.x ) : acos( norm_dir.x ) );

  return longlat;
}

float3 GetSkyViewDirFromLongLat( float2 longlat )
{
  return float3( sin( longlat.x ) * cos( longlat.y ), sin( longlat.y ), cos( longlat.x ) * cos( longlat.y ) );
}

float2 GetSkyViewUVFromLongLat( float2 longlat )
{
  const float l = longlat.y;
  float       v = 0.5f + 0.5f * sign( l ) * sqrt( abs( l ) * 2.0f * kPiInv );
  float       u = longlat.x * kTauInv;
  return float2( u, v );
}

float2 GetSkyViewUVFromDir( float3 dir )
{
  return GetSkyViewUVFromLongLat( GetSkyViewLongLatFromDir( dir ) );
}

#endif // _FUNCTIONS_HLSLI
