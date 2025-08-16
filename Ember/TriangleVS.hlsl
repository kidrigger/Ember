#include "Triangle.hlsli"

VSOut TriangleVS( VSInput IN )
{
  VSOut                  OUT;

  ConstantBuffer<Camera> camera     = ResourceDescriptorHeap[g_Camera];

  float4                 world_pos  = mul( g_Model, IN.Position );
  float4                 screen_pos = mul( camera.View, world_pos );
  screen_pos                        = mul( camera.Projection, screen_pos );

  float3 normal                     = normalize( mul( float4( 2.0f * IN.Normal.xyz - 1.0f, 0.0f ), g_InvModel ).xyz );
  float4 tangent                    = float4(
      normalize( mul( float4( 2.0f * IN.Tangent.xyz - 1.0f, 0.0f ), g_InvModel ).xyz ), 2.0f * IN.Tangent.w - 1.0f );

  OUT.ScreenPosition = screen_pos;
  OUT.Position       = world_pos;
  OUT.Normal         = normal;
  OUT.Tangent        = tangent;
  OUT.Color          = IN.Color;
  OUT.TexCoord[0]    = IN.TexCoord[0];
  OUT.TexCoord[1]    = IN.TexCoord[1];
  return OUT;
}
