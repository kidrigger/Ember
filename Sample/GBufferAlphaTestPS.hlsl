#include "DebugConfig.hlsli"
#include "Triangle.hlsli"
#include "TrianglePSCommon.hlsli"

struct PSOutput
{
  float4 Position : SV_TARGET0;
  float4 Albedo : SV_TARGET1;
  float2 Normal : SV_TARGET2;
  float4 ORM : SV_TARGET3;
  float4 Emissive : SV_TARGET4;
};

PSOutput GBufferAlphaTestPS( PSIn IN )
{
  ConstantBuffer<Camera>     camera    = ResourceDescriptorHeap[g_Camera];
  StructuredBuffer<Material> materials = ResourceDescriptorHeap[g_Materials];

#ifndef STRIP_DEBUG_CONFIG
  ConstantBuffer<DebugConfig> config = ResourceDescriptorHeap[g_ConfigID];
#endif

  Material mat = materials[NonUniformResourceIndex( IN.Material )];

  PSOutput OUT;

  float4   albedo = mat.GetAlbedo( IN.TexCoord, g_DefaultSampler );

  if ( albedo.a < mat.AlphaCutoff ) discard;

  OUT.Position = float4( IN.Position.xyz, mat.EmissiveStrength );
  OUT.Albedo   = IN.Color * albedo;
  OUT.Normal =
      OctahedralEncode( mat.GetNormal( IN.Normal, IN.Tangent, IN.Position.xyz, IN.TexCoord, g_DefaultSampler ) );
  OUT.ORM      = float4( 1.0f, mat.GetMetalRough( IN.TexCoord, g_DefaultSampler ).yx, 0.0f );
  OUT.Emissive = float4( mat.GetRawEmissive( IN.TexCoord, g_DefaultSampler ), 0.0f );

#ifndef STRIP_DEBUG_CONFIG
  if ( config.VisualizationMode == kMeshlet )
  {
    OUT.Albedo = float4( IN.MeshletColor, 1.0f );
  }
#endif

  return OUT;
}
