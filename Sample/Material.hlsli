#ifndef MATERIAL_HLSLI_
#define MATERIAL_HLSLI_

#include "Bindless.hlsli"
#include "Colors.hlsli"

struct Material
{
  ResID         BaseColorTextureIndex;  // 04
  ResID         NormalTextureIndex;     // 08
  ResID         MetalRoughTextureIndex; // 12
  ResID         EmissiveTextureIndex;   // 16
  PackedColor32 BaseColorFactor;        // 20
  PackedColor32 PackedEmissiveFactor;   // 24 // Alpha channel stores texture indexes
  half          EmissiveStrength;       // 26
  half          Metal;                  // 28
  half          Rough;                  // 30
  half          AlphaCutoff;            // 32

  float4        GetAlbedo( in float2 in_texcoord[2], in SamplerState texture_sampler )
  {
    float4 albedo = UnpackColor32( BaseColorFactor );
    if ( IsValidHandle( BaseColorTextureIndex ) )
    {
      uint tex_coord_idx = ((PackedEmissiveFactor >> 24) & 0x3);
      Texture2D texture = ResourceDescriptorHeap[BaseColorTextureIndex];
      return albedo * texture.Sample( texture_sampler, in_texcoord[tex_coord_idx] );
    }
    return albedo;
  }

  float3 GetNormal(
      in float3 in_normal, in float4 in_tangent, in float3 in_position, in float2 in_texcoord[2], in SamplerState texture_sampler )
  {
    float3 normal = normalize( in_normal );
    if ( IsValidHandle( NormalTextureIndex ) )
    {
      uint tex_coord_idx = (PackedEmissiveFactor >> 26) & 0x3;
      
      Texture2D texture   = ResourceDescriptorHeap[NormalTextureIndex];
      float3    normal_ts = texture.Sample( texture_sampler, in_texcoord[tex_coord_idx] ).rgb;
      normal_ts           = normalize( 2.0f * normal_ts - 1.0f );

      float3 tangent;
      float3 bitangent;

      if ( in_tangent.w == 0.0f )
      {
        float3 q1  = ddx( in_position );
        float3 q2  = ddy( in_position );
        float2 st1 = ddx( in_texcoord[tex_coord_idx] );
        float2 st2 = ddy( in_texcoord[tex_coord_idx] );

        float  det = ( st1.x * st2.y - st2.x * st1.y );

        tangent    = -( q1 * st2.y - q2 * st1.y ) / det;
        tangent    = tangent - normal * dot( normal, tangent );
        bitangent  = normalize( cross( normal, tangent ) );
      }
      else
      {
        tangent   = normalize( in_tangent.xyz );
        bitangent = in_tangent.w * cross( normal, tangent );
      }
      normal = normalize( tangent * normal_ts.x + bitangent * normal_ts.y + normal * normal_ts.z );
    }

    return normal;
  }

  float2 GetMetalRough( in float2 in_texcoord[2], in SamplerState texture_sampler )
  {
    if ( IsValidHandle( MetalRoughTextureIndex ) )
    {
      uint tex_coord_idx = (PackedEmissiveFactor >> 28) & 0x3;
      Texture2D texture = ResourceDescriptorHeap[MetalRoughTextureIndex];
      return texture.Sample( texture_sampler, in_texcoord[tex_coord_idx] ).bg * float2( Metal, Rough );
    }
    return float2( Metal, Rough );
  }

  float3 GetEmissive( in float2 in_texcoord[2], in SamplerState texture_sampler )
  {
    float3 emissive = UnpackColor32( PackedEmissiveFactor ).rgb * EmissiveStrength;
    if ( IsValidHandle( EmissiveTextureIndex ) )
    {
      uint tex_coord_idx = (PackedEmissiveFactor >> 30) & 0x3;
      Texture2D texture = ResourceDescriptorHeap[EmissiveTextureIndex];
      return emissive * texture.Sample( texture_sampler, in_texcoord[tex_coord_idx] ).rgb;
    }
    return emissive;
  }

  float3 GetRawEmissive( in float2 in_texcoord[2], in SamplerState texture_sampler )
  {
    float3 emissive = UnpackColor32( PackedEmissiveFactor ).rgb;
    if ( IsValidHandle( EmissiveTextureIndex ) )
    {
      uint tex_coord_idx = (PackedEmissiveFactor >> 30) & 0x3;
      Texture2D texture = ResourceDescriptorHeap[EmissiveTextureIndex];
      return emissive * texture.Sample( texture_sampler, tex_coord_idx ).rgb;
    }
    return emissive;
  }
};

#endif
