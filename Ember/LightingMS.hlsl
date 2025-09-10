#include "Utility.hlsli"

const static float3 kPosition[] = {
  float3( -1.0f, -1.0f, 0.0f ),
  float3( -1.0f, 3.0f, 0.0f ),
  float3( 3.0f, -1.0f, 0.0f ),
};

struct MSOut
{
  float4 Position : SV_Position;
  float2 TexCoord : TEXCOORD;
};

OUTPUT_TOPOLOGY( "triangle" )
NUM_THREADS( 1, 1, 1 )
void LightingMS( uint3 dt_id : SV_DispatchThreadID, out vertices MSOut verts[3], out indices uint3 tris[1] )
{
  SetMeshOutputCounts( 3, 1 );

  [unroll] for ( int i = 0; i < 3; i++ )
  {
    verts[i].Position = float4( kPosition[i], 1.0f );
    verts[i].TexCoord = float2( 0.5f, -0.5f ) * kPosition[i].xy + 0.5f;
  }
  tris[0] = uint3( 0, 1, 2 );
}
