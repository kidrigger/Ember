#include "Bindless.hlsli"
#include "Camera.hlsli"
#include "Math.hlsli"
#include "Utility.hlsli"

cbuffer SSAOBlurInput : register( b0 )
{
  ResID g_SSAOInput;
  ResID g_DepthTex;
  ResID g_OutputTexture;
};

cbuffer FrameConstants : register( b1 )
{
  Camera g_Camera;
};

SamplerState g_ClampedSampler : register( s0 );

NUM_THREADS( 8, 8, 1 )
void SSAOBlurCS( uint3 dispatch_thread_id : SV_DispatchThreadID )
{
  Texture2D<float>   ssao_input  = ResourceDescriptorHeap[g_SSAOInput];
  Texture2D<float>   depth_tex   = ResourceDescriptorHeap[g_DepthTex];
  RWTexture2D<float> out_texture = ResourceDescriptorHeap[g_OutputTexture];

  uint               width;
  uint               height;
  out_texture.GetDimensions( width, height );

  if ( dispatch_thread_id.x >= width || dispatch_thread_id.y >= height ) return;

  float2 screen_scale = 1.0f / float2( width, height );
  float2 uv           = ( dispatch_thread_id.xy + 0.5f ) * screen_scale;
  // uv.y                = 1.0f - uv.y; // Flip y for texture sampling

  float3 pos          = DepthToViewPos( g_Camera.InvProj, depth_tex, uv, g_ClampedSampler );

  float  blur_sum     = 0.0f;
  float  sample_count = 0.0f;

  for ( int y = -2; y <= 2; ++y )
  {
    for ( int x = -2; x <= 2; ++x )
    {
      float2 sample_coord  = uv + float2( x, y ) * screen_scale;
      float  depth_diff    = distance( DepthToViewPos( g_Camera.InvProj, depth_tex, uv, g_ClampedSampler ), pos );
      float  weight        = 1.0f - smoothstep( 0.01f, 0.02f, abs( depth_diff ) );
      blur_sum            += ssao_input.SampleLevel( g_ClampedSampler, sample_coord, 0 ) * weight;

      sample_count        += weight;
    }
  }

  out_texture[dispatch_thread_id.xy] = blur_sum / sample_count;
}
