#include "Bindless.hlsli"
#include "Utility.hlsli"

cbuffer SSAOBlurInput : register( b0 )
{
  ResID g_SSAOInput;
  ResID g_OutputTexture;
};

NUM_THREADS( 8, 8, 1 )
void SSAOBlurCS( uint3 dispatch_thread_id : SV_DispatchThreadID )
{
  Texture2D<float>   ssao_input  = ResourceDescriptorHeap[g_SSAOInput];
  RWTexture2D<float> out_texture = ResourceDescriptorHeap[g_OutputTexture];

  uint               width;
  uint               height;
  out_texture.GetDimensions( width, height );

  if ( dispatch_thread_id.x >= width || dispatch_thread_id.y >= height ) return;

  float blur_sum = 0.0f;

  for ( int y = -2; y <= 2; ++y )
  {
    for ( int x = -2; x <= 2; ++x )
    {
      int2 sample_coord  = int2( dispatch_thread_id.xy ) + int2( x, y );
      sample_coord       = clamp( sample_coord, int2( 0, 0 ), int2( width - 1, height - 1 ) );
      blur_sum          += ssao_input.Load( int3( sample_coord, 0 ) );
    }
  }

  out_texture[dispatch_thread_id.xy] = blur_sum / 25.0f;
}
