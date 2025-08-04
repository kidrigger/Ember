#include "Triangle.hlsli"

struct BackgroundOut
{
  float4 ScreenPosition : SV_POSITION;
  float3 SkyboxCoord : SKYBOX_COORD;
};

float4 BackgroundPS( BackgroundOut IN ) : SV_TARGET0
{
  TextureCube<float3> skybox = ResourceDescriptorHeap[g_Env.Skybox];

  float3              color  = skybox.SampleLevel( g_DefaultSampler, IN.SkyboxCoord, 2.0f );
  return float4( LinearToSrgb( color ), 1.0f );
}
