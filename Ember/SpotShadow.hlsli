#ifndef SPOT_SHADOW_HLSLI_
#define SPOT_SHADOW_HLSLI_

#include "Geometry.hlsli"
#include "LightData.hlsli"

const static float kNearPlane = 0.01f;

cbuffer            QuickTransforms : register( b0 )
{
  DrawList g_DrawList;
  ResID    g_SpotLightBuffer;
  uint     g_LightID;
}

struct SpotShadowPayload
{
  uint MeshletID[32];
  uint FirstTransform;
  uint FirstVertex;
  uint FirstMeshlet;
};

#endif
