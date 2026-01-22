#include "DebugConfig.hlsli"
#include "TrianglePSCommon.hlsli"

struct PSOutput
{
  float4 Position : SV_TARGET0;
  float4 Albedo : SV_TARGET1;
  float2 Normal : SV_TARGET2;
  float4 ORM : SV_TARGET3;
  float4 Emissive : SV_TARGET4;
};

PSOutput GBufferPS( PSIn IN )
{
  StructuredBuffer<Material> materials = ResourceDescriptorHeap[g_DrawBatch.MaterialBuffer];

  Material                   mat       = materials[NonUniformResourceIndex( IN.Material )];

  PSOutput                   OUT;

  OUT.Position = float4( IN.Position.xyz, mat.EmissiveStrength );
  OUT.Albedo   = IN.Color * mat.GetAlbedo( IN.TexCoord, g_DefaultSampler );
  OUT.Normal =
      OctahedralEncode( mat.GetNormal( IN.Normal, IN.Tangent, IN.Position.xyz, IN.TexCoord, g_DefaultSampler ) );
  OUT.ORM      = float4( 1.0f, mat.GetMetalRough( IN.TexCoord, g_DefaultSampler ).yx, 0.0f );
  OUT.Emissive = float4( mat.GetRawEmissive( IN.TexCoord, g_DefaultSampler ), 0.0f );

#ifndef STRIP_DEBUG_CONFIG
  if ( g_Debug.VisualizationMode == kMeshlet )
  {
    OUT.Albedo = float4( IN.MeshletColor, 1.0f );
  }
#endif

  return OUT;
}
