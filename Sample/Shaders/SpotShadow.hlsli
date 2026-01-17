#ifndef SPOT_SHADOW_HLSLI_
#define SPOT_SHADOW_HLSLI_

#include "Geometry.hlsli"
#include "LightData.hlsli"

const static float        kNearPlane = 0.01f;

ConstantBuffer<DrawBatch> g_DrawBatch : register( b0 );

cbuffer                   QuickTransforms : register( b1 )
{
  ResID g_SpotLightBuffer;
  uint  g_LightID;
}

struct SpotShadowPayload
{
  uint MeshletID[32];
  uint InstanceIdx;
  uint VertexLiteStart;
  uint FirstMeshlet;
};

#endif
