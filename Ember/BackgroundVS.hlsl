#include "Triangle.hlsli"

const static float3 kPosition[] = {
  float3( -1.0f, -1.0f, 1.0f ),
  float3( 3.0f, -1.0f, 1.0f ),
  float3( -1.0f, 3.0f, 1.0f ),
};

struct BackgroundOut
{
  float4 ScreenPosition : SV_POSITION;
  float3 SkyboxCoord : SKYBOX_COORD;
};

BackgroundOut BackgroundVS( uint vertex_id : SV_VERTEXID )
{
  BackgroundOut          OUT;

  ConstantBuffer<Camera> camera = ResourceDescriptorHeap[g_Camera];

  OUT.ScreenPosition            = float4( kPosition[vertex_id], 1.0f );

  float4 clip_space             = mul( camera.InvProj, float4( kPosition[vertex_id], 1.0f ) );
  OUT.SkyboxCoord               = mul( camera.InvView, clip_space / clip_space.w ).xyz;

  return OUT;
}
