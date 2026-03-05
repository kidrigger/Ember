#ifndef SPOT_LIGHT_COMMON_HLSLI_
#define SPOT_LIGHT_COMMON_HLSLI_

#include "Camera.hlsli"
#include "DebugConfig.hlsli"
#include "Environment.hlsli"
#include "LightData.hlsli"

struct SpotLightPayload
{
  uint LightID[32];
};

cbuffer FrameConstants : register( b1 )
{
  Camera      g_Camera;
  LightInfo   g_Lights;
  Environment g_Env;
  DebugConfig g_Debug;
}

#endif
