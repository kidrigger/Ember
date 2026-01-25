#include "AtmosphereCommon.hlsli"
#include "Bindless.hlsli"
#include "Colors.hlsli"

cbuffer Atmosphere : register( b1 )
{
  ResID g_Atmosphere;
}

SamplerState g_DefaultSampler : register( s0 );

struct BackgroundOut
{
  float4 ScreenPosition : SV_POSITION;
  float3 SkyboxCoord : SKYBOX_COORD;
};

float4 AtmosphereBackgroundPS( BackgroundOut IN ) : SV_TARGET0
{
  Texture2D<float4> skyview_lut = ResourceDescriptorHeap[g_Atmosphere];

  float3            view_dir    = normalize( IN.SkyboxCoord );

  float2            skyview_uv  = GetSkyViewUVFromDir( view_dir );

  float3            color       = skyview_lut.SampleLevel( g_DefaultSampler, skyview_uv, 0.0f ).rgb;
  color                         = color / ( 1.0f + color );

  return float4( color, 1.0f );
}
