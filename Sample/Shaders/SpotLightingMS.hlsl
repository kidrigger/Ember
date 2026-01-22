#include "Camera.hlsli"
#include "LightData.hlsli"
#include "SpotLightCommon.hlsli"
#include "Utility.hlsli"

#define MAX_VERTS 64
#define MAX_TRIANGLES 124

struct MSIn
{
  uint3 GroupID : SV_GroupID;
  uint3 LocalID : SV_GroupThreadID;
};

uint3 GetBytes( uint2 value, uint sub_offset )
{
  return uint3(
      value[sub_offset >> 2] >> ( ( sub_offset % 4 ) * 8 ) & 0xFF,
      value[( sub_offset + 1 ) >> 2] >> ( ( ( sub_offset + 1 ) % 4 ) * 8 ) & 0xFF,
      value[( sub_offset + 2 ) >> 2] >> ( ( ( sub_offset + 2 ) % 4 ) * 8 ) & 0xFF );
}

static const float2 kVertices[] = {
  float2( -1.0f, -1.0f ),
  float2( 1.0f, -1.0f ),
  float2( 1.0f, 1.0f ),
  float2( -1.0f, 1.0f ),
};

static const uint3 kIndices[] = {
  uint3( 0, 1, 2 ),  uint3( 0, 2, 3 ),  uint3( 1, 5, 6 ),   uint3( 1, 6, 2 ),   uint3( 2, 6, 7 ),
  uint3( 2, 7, 3 ),  uint3( 3, 7, 4 ),  uint3( 3, 4, 0 ),   uint3( 0, 4, 5 ),   uint3( 0, 5, 1 ),
  uint3( 5, 9, 10 ), uint3( 5, 10, 6 ), uint3( 6, 10, 11 ), uint3( 6, 11, 7 ),  uint3( 7, 11, 8 ),
  uint3( 7, 8, 4 ),  uint3( 4, 8, 9 ),  uint3( 4, 9, 5 ),   uint3( 8, 11, 10 ), uint3( 8, 10, 9 ),
};

struct MSVertexOut
{
  float4 ScreenPosition : SV_POSITION;
  float2 TexCoord : TEXCOORD;
};

struct MSPrimitiveOut
{
  uint LightID : LIGHT_ID;
};

OUTPUT_TOPOLOGY( "triangle" )
NUM_THREADS( 32, 1, 1 )
void SpotLightingMS(
    MSIn                          IN,
    in payload SpotLightPayload   lights,
    out vertices MSVertexOut      verts[41],
    out indices uint3             tris[80],
    out primitives MSPrimitiveOut out_light_id[80] )
{
  StructuredBuffer<SpotLight> spot_lights = ResourceDescriptorHeap[g_Lights.SpotLights];

  uint                        light_idx   = lights.LightID[IN.GroupID.x];
  float                       cosine      = spot_lights[light_idx].ConeOuterCutoff;
  float                       slope       = sqrt( 1.0f - cosine * cosine ) / cosine;
  float                       range       = spot_lights[light_idx].Range;

  float                       z_array[]   = { 0.01f / range, cosine, 1.0f };

  float3                      light_dir   = normalize( spot_lights[light_idx].Direction );
  float3 up    = abs( light_dir.y ) < 0.999f ? float3( 0.0f, 1.0f, 0.0f ) : float3( 1.0f, 0.0f, 0.0f );
  float3 right = normalize( cross( light_dir, up ) );
  up           = normalize( cross( right, light_dir ) );

  SetMeshOutputCounts( 12, 20 );

  for ( int i = IN.LocalID.x; i < 12; i += 32 )
  {
    float  z                = z_array[i >> 2];
    float2 local            = lerp( float2( 0.0f, 0.0f ), kVertices[i % 4] * range * slope, min( z, z_array[1] ) );
    float3 ws_volume        = spot_lights[light_idx].Position + light_dir * z * range + right * local.x + up * local.y;
    float4 screen_pos       = mul( g_Camera.Projection, mul( g_Camera.View, float4( ws_volume, 1.0f ) ) );
    verts[i].ScreenPosition = screen_pos;
    verts[i].TexCoord       = ( screen_pos.xy / screen_pos.w ) * float2( 0.5f, -0.5f ) + 0.5f;
  }

  for ( int i = IN.LocalID.x; i < 20; i += 32 )
  {
    out_light_id[i].LightID = light_idx;
    tris[i]                 = kIndices[i];
  }
}
