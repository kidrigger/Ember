#include "OmniShader.hlsli"

FSOut OmniShadowPS( FSIn IN )
{
  FSOut OUT;

  float light_dist = length( IN.WorldPosition.xyz - g_LightPosition );

  light_dist       = light_dist / g_FarPlane;

  OUT.Depth        = light_dist;

  return OUT;
}
