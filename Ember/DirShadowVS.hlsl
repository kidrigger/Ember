#include "LightData.hlsli"

cbuffer QuickTransform : register( b0 )
{
  float4x4 g_Transform;
}

cbuffer LightInfo : register( b1 )
{
  RID  g_LightData;
  uint g_LightIdx;
}

struct VSOut
{
  float4 ScreenPosition : SV_POSITION;
  uint   TargetArrayIndex : SV_RENDERTARGETARRAYINDEX;
};

VSOut DirShadowVS( float4 pos : POSITION, uint instance_idx : SV_INSTANCEID )
{
  StructuredBuffer<DirLight> light_data = ResourceDescriptorHeap[g_LightData];

  // Use the cascade's info.
  float4 v  = mul( light_data[g_LightIdx].LightSpaceMat[instance_idx], mul( g_Transform, pos ) );
  v        /= v.w;
  v.z       = saturate( v.z ); // Saturation allows objects behind the near plane to cast shadows.

  VSOut OUT;
  OUT.ScreenPosition   = v;
  OUT.TargetArrayIndex = instance_idx;
  return OUT;
}
