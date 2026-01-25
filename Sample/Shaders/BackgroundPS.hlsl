#include "Bindless.hlsli"
#include "Colors.hlsli"

cbuffer Skybox : register( b1 )
{
  ResID g_Skybox;
}

SamplerState g_DefaultSampler : register( s0 );

struct BackgroundOut
{
  float4 ScreenPosition : SV_POSITION;
  float3 SkyboxCoord : SKYBOX_COORD;
};

float4 BackgroundPS( BackgroundOut IN ) : SV_TARGET0
{
  TextureCube<float3> skybox = ResourceDescriptorHeap[g_Skybox];

  float3              color  = skybox.SampleLevel( g_DefaultSampler, normalize( IN.SkyboxCoord ), 0.0f );
  return float4( color, 1.0f );
}
