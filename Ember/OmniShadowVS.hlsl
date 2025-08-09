#include "OmniShader.hlsli"

VSOut OmniShadowVS( float4 position : POSITION )
{
  VSOut OUT;
  OUT.WorldPosition  = mul( g_Transform, position );
  OUT.ScreenPosition = mul( g_ProjView, OUT.WorldPosition );

  return OUT;
}
