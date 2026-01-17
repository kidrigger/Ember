#include "Bindless.hlsli"
#include "Camera.hlsli"
#include "Utility.hlsli"

cbuffer BackgroundCB : register( b0 )
{
  ResID g_Camera;
  ResID g_Skybox;
}

struct BackgroundOut
{
  float4 ScreenPosition : SV_POSITION;
  float3 SkyboxCoord : SKYBOX_COORD;
};

const static float3 kPosition[] = {
  float3( 3.0f, -1.0f, 1.0f ),
  float3( -1.0f, -1.0f, 1.0f ),
  float3( -1.0f, 3.0f, 1.0f ),
};

BackgroundOut BackgroundVS( uint vertex_id : SV_VERTEXID )
{
  BackgroundOut          OUT;

  ConstantBuffer<Camera> camera = ResourceDescriptorHeap[g_Camera];

  OUT.ScreenPosition            = float4( kPosition[vertex_id], 1.0f );

  float4 clip_space             = mul( camera.InvProj, float4( kPosition[vertex_id], 1.0f ) );
  OUT.SkyboxCoord               = mul( camera.InvView, clip_space / clip_space.w ).xyz - camera.Position.xyz;

  return OUT;
}
