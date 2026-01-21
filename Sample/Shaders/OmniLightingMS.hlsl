#include "Camera.hlsli"
#include "LightData.hlsli"
#include "OmniLightCommon.hlsli"
#include "Utility.hlsli"

cbuffer BindlessIndex : register( b1 )
{
  ResID g_Camera;
  ResID g_ConfigID;
  ResID g_PointLights;
  uint  g_ShadowPointLightCount;
  uint  g_PointLightCount;
  ResID g_DirLights;
  uint  g_ShadowDirLightCount;
  uint  g_DirLightCount;
  ResID g_SpotLights;
  uint  g_ShadowSpotLightCount;
  uint  g_SpotLightCount;
}

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

// Create a vertex buffer and index buffer for a basic icosahedron
static const float3 kVertices[] = {
  float3( -1.0f, 0.0f, 1.6180339887f ), float3( 1.0f, 0.0f, 1.6180339887f ),   float3( -1.0f, 0.0f, -1.6180339887f ),
  float3( 1.0f, 0.0f, -1.6180339887f ), float3( 0.0f, 1.6180339887f, 1.0f ),   float3( 0.0f, 1.6180339887f, -1.0f ),
  float3( 0.0f, -1.6180339887f, 1.0f ), float3( 0.0f, -1.6180339887f, -1.0f ), float3( 1.6180339887f, 1.0f, 0.0f ),
  float3( -1.6180339887f, 1.0f, 0.0f ), float3( 1.6180339887f, -1.0f, 0.0f ),  float3( -1.6180339887f, -1.0f, 0.0f ),
};
static const uint3 kIndices[] = {
  uint3( 1, 4, 0 ),  uint3( 4, 9, 0 ),  uint3( 4, 5, 9 ),  uint3( 8, 5, 4 ),  uint3( 1, 8, 4 ),
  uint3( 1, 10, 8 ), uint3( 10, 3, 8 ), uint3( 8, 3, 5 ),  uint3( 3, 2, 5 ),  uint3( 3, 7, 2 ),
  uint3( 3, 10, 7 ), uint3( 10, 6, 7 ), uint3( 6, 11, 7 ), uint3( 6, 0, 11 ), uint3( 6, 1, 0 ),
  uint3( 10, 1, 6 ), uint3( 11, 0, 9 ), uint3( 2, 11, 9 ), uint3( 5, 2, 9 ),  uint3( 11, 2, 7 ),
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
void OmniLightingMS(
    MSIn                          IN,
    in payload OmniLightPayload   lights,
    out vertices MSVertexOut      verts[12],
    out indices uint3             tris[20],
    out primitives MSPrimitiveOut out_light_id[20] )
{
  StructuredBuffer<PointLight> point_lights = ResourceDescriptorHeap[g_PointLights];
  ConstantBuffer<Camera>       camera       = ResourceDescriptorHeap[g_Camera];

  uint                         light_idx    = lights.LightID[IN.GroupID.x];

  SetMeshOutputCounts( 12, 20 );

  for ( int i = IN.LocalID.x; i < 12; i += 32 )
  {
    // Correct it so unit sphere is inside the light volume
    float4 local = float4(
        normalize( kVertices[i] ) * 1.323169f * point_lights[light_idx].Range + point_lights[light_idx].Position,
        1.0f );
    float4 screen_pos       = mul( camera.Projection, mul( camera.View, local ) );
    verts[i].ScreenPosition = screen_pos;
    verts[i].TexCoord       = screen_pos.xy / screen_pos.w * float2( 0.5f, -0.5f ) + 0.5f;
  }

  for ( int i = IN.LocalID.x; i < 20; i += 32 )
  {
    out_light_id[i].LightID = light_idx;
    tris[i]                 = kIndices[i];
  }
}
