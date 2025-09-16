#ifndef LIGHT_DATA_HLSLI_
#define LIGHT_DATA_HLSLI_

#include "Bindless.hlsli"
#include "Colors.hlsli"

#define NUM_CASCADES 6

struct PointLight
{
  float3        Position;    // 12
  float         Range;       // 16
  PackedColor32 Color;       // 20
  float         Intensity;   // 24
  float         Attenuation; // 28
  ResID         ShadowIdx;   // 32

  float3        GetRadiance()
  {
    return Intensity * UnpackColor32( Color ).rgb;
  }
};

struct SpotLight
{
  float3        Position;        // 12
  float         Range;           // 16
  float3        Direction;       // 28
  PackedColor32 Color;           // 32
  float         Intensity;       // 36
  float         ConeInnerCutoff; // 40
  float         ConeOuterCutoff; // 44
  ResID         ShadowMap;       // 48

  float3        GetRadiance()
  {
    return Intensity * UnpackColor32( Color ).rgb;
  }
};

struct DirLight
{
  float4x4      LightSpaceMat[NUM_CASCADES]; // 384
  float3        Direction;                   // 396
  PackedColor32 Color;                       // 400
  float         Intensity;                   // 404
  ResID         ShadowIdx;                   // 408
  uint          Pad0;                        // 412
  uint          Pad1;                        // 416
  float4        CascadesSph[NUM_CASCADES];   // 512

  float3        GetRadiance()
  {
    return Intensity * UnpackColor32( Color ).rgb;
  }
};

#endif
