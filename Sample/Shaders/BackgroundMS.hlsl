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
  float3( -1.0f, -1.0f, 1.0f ),
  float3( 3.0f, -1.0f, 1.0f ),
  float3( -1.0f, 3.0f, 1.0f ),
};

OUTPUT_TOPOLOGY( "triangle" )
NUM_THREADS( 1, 1, 1 )
void BackgroundMS( uint3 dt_id : SV_DispatchThreadID, out vertices BackgroundOut verts[3], out indices uint3 tris[1] )
{
  SetMeshOutputCounts( 3, 1 );

  for ( int i = 0; i < 3; i++ )
  {
    float4 screen_pos       = float4( kPosition[i], 1.0f );

    float4 clip_space       = mul( g_Camera.InvProj, screen_pos );
    float3 skybox_coord     = mul( g_Camera.InvView, clip_space / clip_space.w ).xyz - g_Camera.Position.xyz;

    verts[i].ScreenPosition = screen_pos;
    verts[i].SkyboxCoord    = skybox_coord;
  }

  tris[0] = uint3( 0, 1, 2 );
}
