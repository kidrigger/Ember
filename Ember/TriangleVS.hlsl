#include "Triangle.hlsli"

VSOut TriangleVS( VSInput IN )
{
  VSOut                  OUT;

  ConstantBuffer<Camera> camera     = ResourceDescriptorHeap[g_Camera];

  float4                 world_pos  = mul( g_Model, float4( IN.Position, 1.0f ) );
  float4                 screen_pos = mul( camera.View, world_pos );
  screen_pos                        = mul( camera.Projection, screen_pos );

  float4 tangent                    = select(
      IN.Tangent.w == 0,
      0.0f.xxxx,
      float4( normalize( mul( float4( IN.Tangent.xyz, 0.0f ), g_InvModel ).xyz ), IN.Tangent.w ) );

  OUT.ScreenPosition = screen_pos;
  OUT.Position       = world_pos;
  OUT.Normal         = normalize( mul( float4( IN.Normal, 0.0f ), g_InvModel ).xyz );
  OUT.Tangent        = tangent;
  OUT.Color          = float4( IN.Color, 1.0f );
  OUT.TexCoord[0]    = IN.TexCoord[0];
  OUT.TexCoord[1]    = IN.TexCoord[1];
  return OUT;
}
