#include "Bindless.hlsli"
#include "Camera.hlsli"
#include "Colors.hlsli"
#include "DebugConfig.hlsli"
#include "Environment.hlsli"
#include "LightData.hlsli"

cbuffer FrameConstants : register( b0 )
{
  Camera      g_Camera;
  LightInfo   g_Lights;
  Environment g_Env;
  DebugConfig g_Debug;
}

SamplerState g_DefaultSampler : register( s0 );

struct BackgroundOut
{
  float4 ScreenPosition : SV_POSITION;
  float3 SkyboxCoord : SKYBOX_COORD;
};

float4 BackgroundPS( BackgroundOut IN ) : SV_TARGET0
{
  TextureCube<float3> skybox = ResourceDescriptorHeap[g_Env.Skybox];

  float3              color  = skybox.SampleLevel( g_DefaultSampler, normalize( IN.SkyboxCoord ), 0.0f );
  return float4( color, 1.0f );
}
