#include "Bindless.hlsli"
#include "Camera.hlsli"
#include "Utility.hlsli"

ConstantBuffer<Camera> g_Camera : register( b0 );

ResID                  g_Skybox : register( b0 );

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
  BackgroundOut OUT;

  OUT.ScreenPosition = float4( kPosition[vertex_id], 1.0f );

  float4 clip_space  = mul( g_Camera.InvProj, float4( kPosition[vertex_id], 1.0f ) );
  OUT.SkyboxCoord    = mul( g_Camera.InvView, clip_space / clip_space.w ).xyz - g_Camera.Position.xyz;

  return OUT;
}
