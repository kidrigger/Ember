#include "OmniShader.hlsli"

VSOut OmniShadowVS( float4 position : POSITION, uint instance_id : SV_INSTANCEID )
{
  ConstantBuffer<ProjectionTransforms> proj_view = ResourceDescriptorHeap[g_ProjViewID];

  float                                f         = g_FarPlane;
  float                                n         = 0.01f;

  VSOut                                OUT;
  OUT.WorldPosition = mul( g_Transform, position );

  float4 pos        = mul( proj_view.Views[instance_id], float4( OUT.WorldPosition.xyz - g_LightPosition, 1.0f ) );

  // Manually calculating the projection
  OUT.ScreenPosition = float4( pos.x, pos.y, pos.z * f / ( f - n ) - pos.w * n * f / ( f - n ), pos.z );
  OUT.Layer          = instance_id;

  return OUT;
}
