#include "Utility.hlsli"

const static float2 kPosition[] = {
  float2( -1.0f, -1.0f ),
  float2( 3.0f, -1.0f ),
  float2( -1.0f, 3.0f ),
};

struct VSOut
{
  float4 Position : SV_POSITION;
  float2 TexCoord : TEXCOORD;
};

VSOut ScreenSpaceTriangleVS( uint vertex_id : SV_VERTEXID )
{
  VSOut OUT;
  OUT.Position = float4( kPosition[vertex_id], 1.0f, 1.0f );
  OUT.TexCoord = 0.5f + float2( 0.5f, -0.5f ) * kPosition[vertex_id].xy;
  return OUT;
}
