#include "DebugConfig.hlsli"
#include "DepthPrePass.hlsli"

void DepthPrePassMaskedPS( PSIn IN )
{
  StructuredBuffer<Material> materials = ResourceDescriptorHeap[g_DrawBatch.MaterialBuffer];

  Material                   mat       = materials[NonUniformResourceIndex( IN.Material )];

  float                      alpha     = mat.GetAlbedo( IN.TexCoord, g_DefaultSampler ).a;

  if ( alpha < mat.AlphaCutoff ) discard;
}
