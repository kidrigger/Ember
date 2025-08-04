#include "Bindless.hlsli"
#include "IBLCommon.hlsli"
#include "PBR.hlsli"

const static uint kSampleCount     = 2048u;
const static uint kMaxPrefilterLoD = 5;

cbuffer           Block : register( b0, space0 )
{
  RID   g_Skybox;
  uint  g_SkyboxSide;
  RID   g_OutputTextureHandle;
  uint  g_OutputSide;
  float g_Roughness;
}

SamplerState g_Sampler : register( s0, space0 );

float        GetSampleMipLevel( float n_dot_h, float h_dot_v, float sample_count )
{
  float d                = TrowbridgeReitzGGX( n_dot_h, g_Roughness );
  float pdf              = ( d * n_dot_h / ( 4.0f * h_dot_v ) ) + 0.0001f;

  float surf_area_texel  = 4.0f * kPi / ( 6.0f * g_SkyboxSide * g_SkyboxSide );
  float surf_area_sample = 1.0f / ( sample_count * pdf + 0.0001f );

  return g_Roughness == 0.0f ? 0.0f : 0.5f * log2( surf_area_sample / surf_area_texel );
}

NUMTHREADS( 16, 16, 1 )
void Prefilter( uint3 global_invocation_id : SV_DispatchThreadID )
{
  float3                   normal          = GetCubeDir( global_invocation_id.xy, global_invocation_id.z, 1.0f / g_OutputSide );
  float3                   view_dir        = normal;

  TextureCube              skybox          = ResourceDescriptorHeap[g_Skybox];
  RWTexture2DArray<float3> output_tex      = ResourceDescriptorHeap[g_OutputTextureHandle];

  float                    total_weight    = 0.0f;
  float3                   prefilter_color = 0.0f;
  for ( uint i = 0u; i < kSampleCount; ++i )
  {
    float2 xi        = Hammersley( i, kSampleCount );
    float3 halfway   = ImportanceSampleGGX( xi, normal, g_Roughness );
    float3 light_dir = normalize( 2.0 * dot( view_dir, halfway ) * halfway - view_dir );

    float  n_dot_h   = max( dot( normal, halfway ), 0.0f );

    float  mip_level = GetSampleMipLevel( n_dot_h, n_dot_h /* N = V :: NdotH = HdotV */, kSampleCount );

    float  n_dot_l   = max( dot( normal, light_dir ), 0.0 );
    if ( n_dot_l > 0.0 )
    {
      prefilter_color += skybox.SampleLevel( g_Sampler, light_dir, mip_level ).rgb * n_dot_l;
      total_weight    += n_dot_l;
    }
  }
  prefilter_color                  = prefilter_color / total_weight;

  output_tex[global_invocation_id] = prefilter_color;
}
