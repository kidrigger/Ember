#ifndef ENVIRONMENT_HLSLI_
#define ENVIRONMENT_HLSLI_

#include "Bindless.hlsli"

struct Environment
{
  ResID  Skybox;
  ResID  DiffuseIrradiance;
  ResID  PrefilterMap;
  ResID  BrdfLUT;

  float3 SampleIrradiance( float3 direction, SamplerState sam )
  {
    if ( IsValidHandle( DiffuseIrradiance ) )
    {
      TextureCube diff_irr = ResourceDescriptorHeap[DiffuseIrradiance];
      return diff_irr.Sample( sam, direction ).rgb;
    }
    return 0.04f;
  }

  float3 SamplePrefiltered( float3 direction, float roughness, SamplerState sam )
  {
    const static float kMaxMipLevel = 5.0f;
    if ( IsValidHandle( PrefilterMap ) )
    {
      float       mip       = kMaxMipLevel * roughness;
      TextureCube prefilter = ResourceDescriptorHeap[PrefilterMap];
      return prefilter.SampleLevel( sam, direction, mip ).rgb;
    }
    return 0.0f;
  }

  float2 SampleBrdfLut( float n_dot_v, float roughness, SamplerState sam )
  {
    if ( IsValidHandle( BrdfLUT ) )
    {
      Texture2D<float2> brdf_lut = ResourceDescriptorHeap[BrdfLUT];
      return brdf_lut.Sample( sam, float2( n_dot_v, roughness ) );
    }
    return 0.0f;
  }
};

#endif
