#define NUMTHREADS( X, Y, Z ) [numthreads( X, Y, Z )]

SamplerState g_BilinearSampler : register( s0, space0 );

cbuffer      InputOutput : register( b0, space0 )
{
  float2 g_TexelSize;
  uint   g_InputIndex;
  uint   g_OutputIndex;
}

NUMTHREADS( 8, 8, 1 )
void MipMap( uint3 dt_id : SV_DispatchThreadID )
{
  float2              tex_coord = g_TexelSize * ( dt_id.xy + 0.5f );

  Texture2D           src       = ResourceDescriptorHeap[g_InputIndex];
  RWTexture2D<float4> dst       = ResourceDescriptorHeap[g_OutputIndex];

  float4              color     = src.SampleLevel( g_BilinearSampler, tex_coord, 0 );
  dst[dt_id.xy]                 = color;
}
