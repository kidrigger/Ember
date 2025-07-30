#include "Triangle.hlsli"

float4 TrianglePS( FSIn IN ) : SV_TARGET0
{
  float4 out_color = 1.0f;
  if ( ( g_Material.BaseColorTextureIndex != kInvalidIndex ) && ( g_Material.SamplerIndex != kInvalidIndex ) )
  {
    Texture2D    texture         = ResourceDescriptorHeap[g_Material.BaseColorTextureIndex];
    SamplerState texture_sampler = SamplerDescriptorHeap[g_Material.SamplerIndex];
    out_color                    = texture.Sample( texture_sampler, IN.TexCoord );
  }
  out_color *= Unpack( g_Material.BaseColorFactor );
  return pow( out_color, 1.0f / 2.2f );
}
