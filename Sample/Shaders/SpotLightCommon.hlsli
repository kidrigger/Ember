#ifndef SPOT_LIGHT_COMMON_HLSLI_
#define SPOT_LIGHT_COMMON_HLSLI_

struct SpotLightPayload
{
  uint LightID[32];
};

cbuffer BindlessIndex : register( b1 )
{
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

#endif
