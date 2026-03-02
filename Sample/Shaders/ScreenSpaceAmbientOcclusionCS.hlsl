#include "Bindless.hlsli"
#include "Camera.hlsli"
#include "Math.hlsli"
#include "Quantization.hlsli"
#include "Utility.hlsli"

cbuffer SSAOConstants : register( b0 )
{
  ResID g_Depth;
  ResID g_Kernel;
  ResID g_RandomDirs;
  ResID g_OutTexture;
};

cbuffer FrameConstants : register( b1 )
{
  Camera g_Camera;
};

SamplerState g_DefaultSampler : register( s0 );

NUM_THREADS( 8, 8, 1 )
void ScreenSpaceAmbientOcclusionCS( uint3 local_id : SV_GroupThreadID, uint3 dispatch_thread_id : SV_DispatchThreadID )
{
  Texture2D<float>   depth_tex    = ResourceDescriptorHeap[g_Depth];
  ByteAddressBuffer  kernel       = ResourceDescriptorHeap[g_Kernel];
  ByteAddressBuffer  random_dirs  = ResourceDescriptorHeap[g_RandomDirs];
  RWTexture2D<float> out_tex      = ResourceDescriptorHeap[g_OutTexture];

  float              width, height;
  out_tex.GetDimensions( width, height );

  if ( dispatch_thread_id.x >= ( uint )width || dispatch_thread_id.y >= ( uint )height ) return;

  float2 screen_scale  = 1.0f / float2( width, height );
  float2 uv            = ( float2( dispatch_thread_id.xy ) + 0.5f ) * screen_scale;

  float3 position_view = DepthToViewPos( g_Camera.InvProj, depth_tex, uv, g_DefaultSampler );
  float3 normal_depth  = DepthToNormal( g_Camera.InvProj, depth_tex, uv, g_DefaultSampler );

  float4 rotation =
      asfloat( random_dirs.Load4( ( SimpleHash( dispatch_thread_id.x + dispatch_thread_id.y * width ) % 64 ) * 16 ) );

  float radius    = 0.5f;

  float occlusion = 0.0f;
  float samples   = 0.0f;

  for ( int i = 0; i < 64; i++ )
  {
    float3 sample           = asfloat( kernel.Load3( i * 3 * 4 ) );

    sample                  = MulQuatVec( rotation, sample );
    sample                  = dot( sample, normal_depth ) < 0.0f ? -sample : sample;
    float3 sample_pos_view  = position_view + sample * radius;
    float4 sample_pos_clip  = mul( g_Camera.Projection, float4( sample_pos_view, 1.0f ) );
    sample_pos_clip.xyz    /= sample_pos_clip.w;

    if ( abs( sample_pos_clip.x ) > 1.0f || abs( sample_pos_clip.y ) > 1.0f )
    {
      continue; // Skip samples outside the screen
    }

    float2 depth_uv    = sample_pos_clip.xy * 0.5f + 0.5f;
    depth_uv.y         = 1.0f - depth_uv.y; // Flip y for texture sampling

    float sampled_d    = DepthToViewPos( g_Camera.InvProj, depth_tex, depth_uv, g_DefaultSampler ).z;

    float range_check  = smoothstep( 0.0f, 1.0f, radius / abs( position_view.z - sampled_d ) );
    occlusion         += ( sampled_d >= sample_pos_view.z + 0.025f ? 1.0f : 0.0f ) * range_check;
    samples           += 1.0f;
  }

  occlusion                      = 1.0f - ( occlusion / max( samples, 1.0f ) ); // Normalize and invert occlusion

  out_tex[dispatch_thread_id.xy] = occlusion;
}
