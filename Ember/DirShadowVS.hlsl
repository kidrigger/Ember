cbuffer QuickTransforms : register( b0 )
{
  float4x4 g_Transform;
  float4x4 g_ProjView;
}

float4 DirShadowVS( float4 pos : POSITION ) : SV_POSITION
{
  float4 v  = mul( g_ProjView, mul( g_Transform, pos ) );
  v        /= v.w;
  v.z       = saturate( v.z ); // Saturation allows objects behind the near plane to cast shadows.
  return v;
}
