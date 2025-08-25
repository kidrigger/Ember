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
};

struct DirLight
{
  float4x4      LightSpaceMat[NUM_CASCADES]; // 384
  float3        Direction;                   // 396
  float4        Cascades0To4;                // 412
  float         Cascade5;                    // 416
  PackedColor32 Color;                       // 420
  float         Intensity;                   // 424
  ResID         ShadowIdx;                   // 428
  uint          Pad;                         // 432
};

#endif
