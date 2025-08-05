#include "Bindless.hlsli"
#include "Utility.hlsli"

cbuffer Block
{
  RID  g_EnvCubeHandle;
  RID  g_OutputCubeHandle;
  uint g_CubeSide;
}

SamplerState g_Sampler : register( s0, space0 );

NUMTHREADS( 16, 16, 1 )
void DiffuseIrradiance( uint3 global_invocation_id : SV_DispatchThreadID )
{
  float3 forward = GetCubeDir( float2( global_invocation_id.xy ), global_invocation_id.z, 1.0f / g_CubeSide );
  float3 up      = abs( forward.y ) < 1.0f ? float3( 0.0f, 1.0f, 0.0f ) : float3( 1.0f, 0.0f, 0.0f ); // 0.01f offset to
  float3 right   = normalize( cross( up, forward ) );
  up             = normalize( cross( forward, right ) );

  float3                   irradiance   = 0.0f;
  float                    sample_step  = 0.005f;
  float                    sample_count = 0.0f;

  TextureCube              skybox       = ResourceDescriptorHeap[g_EnvCubeHandle];
  RWTexture2DArray<float3> diffuse_tex  = ResourceDescriptorHeap[g_OutputCubeHandle];

  for ( float azimuth = 0.0f; azimuth < kTau; azimuth += sample_step )
  {
    for ( float zenith = 0.0f; zenith < kHalfPi; zenith += sample_step )
    {
      float3 direction_tan_space =
          float3( sin( zenith ) * cos( azimuth ), sin( zenith ) * sin( azimuth ), cos( zenith ) );
      float3 direction_world =
          direction_tan_space.x * right + direction_tan_space.y * up + direction_tan_space.z * forward;

      irradiance += skybox.SampleLevel( g_Sampler, direction_world, 0 ).xyz * ( cos( zenith ) * sin( zenith ) );
      sample_count++;
    }
  }

  diffuse_tex[global_invocation_id] = kPi * irradiance * ( 1.0f / sample_count );
}
