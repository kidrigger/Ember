#include "Triangle.hlsli"

float4 TrianglePS( FSIn IN ) : SV_TARGET0
{
  float4 out_color = 1.0f;
  if ( ( g_TextureIndex != 0xFFFFFFFF ) && ( g_SamplerIndex != 0xFFFFFFFF ) )
  {
    Texture2D    texture         = ResourceDescriptorHeap[g_TextureIndex];
    SamplerState texture_sampler = SamplerDescriptorHeap[g_SamplerIndex];
    out_color                    = texture.SampleLevel( texture_sampler, IN.TexCoord, 0 );
  }
  out_color *= g_BaseColor;
  return pow( out_color, 1.0f / 2.2f );
}
