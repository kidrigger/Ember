#ifndef ENVIRONMENT_HLSLI_
#define ENVIRONMENT_HLSLI_

#include "Bindless.hlsli"
#include "SpatialHashMap.hlsli"

struct ReflectionProbe
{
  half4  PositionRadius; // xyz = position, w = radius
  ResID  PrefilterMap;

  float3 GetPosition()
  {
    return PositionRadius.xyz;
  }

  float3 ParallaxCorrection( in float3 refl_dir, in float3 position )
  {
    float3 bound_max = PositionRadius.xyz + PositionRadius.www;
    float3 bound_min = PositionRadius.xyz - PositionRadius.www;

    float3 t_max     = ( bound_max - position ) / refl_dir;
    float3 t_min     = ( bound_min - position ) / refl_dir;
    float3 t_far     = max( t_max, t_min );
    float  t         = min( t_far.x, min( t_far.y, t_far.z ) );

    float3 intersect = position + refl_dir * t;
    refl_dir         = intersect - PositionRadius.xyz;

    return refl_dir;
  }

  float3 SamplePrefiltered( float3 direction, float3 position, float roughness, SamplerState sam )
  {
    const static float kMaxMipLevel = 5.0f;
    if ( !IsValidHandle( NonUniformResourceIndex( PrefilterMap ) ) ) return 0.0f;

    direction             = ParallaxCorrection( direction, position );

    float       mip       = kMaxMipLevel * roughness;
    TextureCube prefilter = ResourceDescriptorHeap[NonUniformResourceIndex( PrefilterMap )];
    return prefilter.SampleLevel( sam, direction, mip ).rgb;
  }
};

#define ReflectionProbe_size 12

struct Environment
{
  ResID  Skybox;
  ResID  DiffuseIrradiance;
  ResID  PrefilterMap;
  ResID  BrdfLUT;
  ResID  IBLProbes;
  ResID  CellProbeMap;
  uint   CellProbeMapSlotCount;
  float  CellSize;

  float3 SampleIrradiance( float3 direction, SamplerState sam )
  {
    if ( !IsValidHandle( DiffuseIrradiance ) ) return 0.04f;

    TextureCube diff_irr = ResourceDescriptorHeap[DiffuseIrradiance];
    return diff_irr.Sample( sam, direction ).rgb;
  }

  float3 SamplePrefiltered( float3 direction, float roughness, SamplerState sam )
  {
    const static float kMaxMipLevel = 5.0f;
    if ( !IsValidHandle( PrefilterMap ) ) return 0.0f;

    float       mip       = kMaxMipLevel * roughness;
    TextureCube prefilter = ResourceDescriptorHeap[PrefilterMap];
    return prefilter.SampleLevel( sam, direction, mip ).rgb;
  }

  float2 SampleBrdfLut( float n_dot_v, float roughness, SamplerState sam )
  {
    if ( !IsValidHandle( BrdfLUT ) ) return 0.0f;

    Texture2D<float2> brdf_lut = ResourceDescriptorHeap[BrdfLUT];
    return brdf_lut.Sample( sam, float2( n_dot_v, roughness ) );
  }

  bool LoadProbe( out ReflectionProbe probe, in StructuredBuffer<ReflectionProbe> ibl_probes, in uint probe_idx )
  {
    if ( probe_idx == kInvalidIndex ) return false;

    probe = ibl_probes[probe_idx];
    return true;
  }

  bool QueryProbe( out ReflectionProbe probe, in float3 position )
  {
    if ( !IsValidHandle( CellProbeMap ) || CellProbeMapSlotCount == 0 || !IsValidHandle( IBLProbes ) ) return false;

    ByteAddressBuffer cell_probe_map = ResourceDescriptorHeap[CellProbeMap];
    uint              index = SpatialHashMap_Fetch( cell_probe_map, CellProbeMapSlotCount, position, CellSize );

    if ( index == kInvalidIndex ) return false;

    StructuredBuffer<ReflectionProbe> ibl_probes = ResourceDescriptorHeap[IBLProbes];
    probe                                        = ibl_probes[index];

    return true;
  }
};

#endif
