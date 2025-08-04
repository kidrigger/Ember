#include "IBLCommon.hlsli"
#include "PBR.hlsli"

const static uint kSampleCount = 1024u;

cbuffer           Block : register( b0, space0 )
{
  uint g_OutputTextureHandle;
  uint g_Width;
  uint g_Height;
}

float2 IntegrateBRDF( float n_dot_v, float roughness )
{
  float3 view_dir;
  view_dir.x    = sqrt( 1.0f - n_dot_v * n_dot_v );
  view_dir.y    = 0.0f;
  view_dir.z    = n_dot_v;

  float  a      = 0.0f;
  float  b      = 0.0f;

  float3 normal = float3( 0.0f, 0.0f, 1.0f );

  for ( uint i = 0u; i < kSampleCount; ++i )
  {
    float2 xi        = Hammersley( i, kSampleCount );
    float3 halfway   = ImportanceSampleGGX( xi, normal, roughness );
    float3 light_dir = normalize( 2.0f * dot( view_dir, halfway ) * halfway - view_dir );


    float  n_dot_l   = max( light_dir.z, 0.0f );
    float  n_dot_h   = max( halfway.z, 0.0f );
    float  v_dot_h   = max( dot( view_dir, halfway ), 0.0f );

    if ( n_dot_l > 0.0f )
    {
      float geom      = IBLGeometrySmith( n_dot_v, n_dot_l, roughness );
      float geom_vis  = ( geom * v_dot_h ) / max( ( n_dot_h * n_dot_v ), 0.0001f );
      float fc        = pow( 1.0f - v_dot_h, 5.0f );

      a              += ( 1.0f - fc ) * geom_vis;
      b              += fc * geom_vis;
    }
  }
  a /= float( kSampleCount );
  b /= float( kSampleCount );

  return float2( a, b );
}

NUMTHREADS( 16, 16, 1 )
void BrdfLUT( uint3 global_invocation_id : SV_DispatchThreadID )
{
  float2              uv                   = global_invocation_id.xy / float2( g_Width - 1, g_Height - 1 );

  RWTexture2D<float2> storage_texture      = ResourceDescriptorHeap[g_OutputTextureHandle];

  float2              integrated_brdf      = IntegrateBRDF( uv.x, uv.y );
  storage_texture[global_invocation_id.xy] = integrated_brdf;
}
