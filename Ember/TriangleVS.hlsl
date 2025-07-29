#include "Triangle.hlsli"

VSOut TriangleVS( VSInput IN )
{
  VSOut                  OUT;

  ConstantBuffer<Camera> camera = ResourceDescriptorHeap[g_Camera];

  float4                 pos    = mul( g_Model, float4( IN.Position, 1.0f ) );
  pos                           = mul( camera.View, pos );
  pos                           = mul( camera.Projection, pos );
  OUT.Position                  = pos;
  OUT.Color                     = float4( IN.Color, 1.0f );
  OUT.TexCoord                  = IN.TexCoord0;
  return OUT;
}
