#include "Triangle.hlsli"

float4 TrianglePS( FSIn IN ) : SV_TARGET0
{
  Texture2D    texture         = ResourceDescriptorHeap[g_TextureIndex];
  SamplerState texture_sampler = SamplerDescriptorHeap[0];
  return pow( texture.SampleLevel( texture_sampler, IN.TexCoord, 0 ), 1.0f / 2.2f );
}
