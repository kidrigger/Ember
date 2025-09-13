#include "Camera.hlsli"
#include "LightData.hlsli"
#include "OmniLightCommon.hlsli"
#include "Utility.hlsli"

#define MAX_VERTS 64
#define MAX_TRIANGLES 124

cbuffer BindlessIndex : register( b1 )
{
  ResID g_Materials;
  ResID g_Camera;
  ResID g_PointLights;
  uint  g_PointLightCount;
  uint  g_ShadowPointLightCount;
  ResID g_DirLights;
  uint  g_DirLightCount;
  uint  g_ShadowDirLightCount;
  ResID g_ConfigID;
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

static const float3 kVertices[] = {
  float3( 0.0, -1.0, 0.0 ),
  float3( 0.723607, -0.44722, 0.525725 ),
  float3( -0.276388, -0.44722, 0.850649 ),
  float3( -0.894426, -0.447216, 0.0 ),
  float3( -0.276388, -0.44722, -0.850649 ),
  float3( 0.723607, -0.44722, -0.525725 ),
  float3( 0.276388, 0.44722, 0.850649 ),
  float3( -0.723607, 0.44722, 0.525725 ),
  float3( -0.723607, 0.44722, -0.525725 ),
  float3( 0.276388, 0.44722, -0.850649 ),
  float3( 0.894426, 0.447216, 0.0 ),
  float3( 0.0, 1.0, 0.0 ),
  float3( -0.162456, -0.850654, 0.499995 ),
  float3( 0.425323, -0.850654, 0.309011 ),
  float3( 0.262869, -0.525738, 0.809012 ),
  float3( 0.850648, -0.525736, 0.0 ),
  float3( 0.425323, -0.850654, -0.309011 ),
  float3( -0.52573, -0.850652, 0.0 ),
  float3( -0.688189, -0.525736, 0.499997 ),
  float3( -0.162456, -0.850654, -0.499995 ),
  float3( -0.688189, -0.525736, -0.499997 ),
  float3( 0.262869, -0.525738, -0.809012 ),
  float3( 0.951058, 0.0, 0.309013 ),
  float3( 0.951058, 0.0, -0.309013 ),
  float3( 0.0, 0.0, 1.0 ),
  float3( 0.587786, 0.0, 0.809017 ),
  float3( -0.951058, 0.0, 0.309013 ),
  float3( -0.587786, 0.0, 0.809017 ),
  float3( -0.587786, 0.0, -0.809017 ),
  float3( -0.951058, 0.0, -0.309013 ),
  float3( 0.587786, 0.0, -0.809017 ),
  float3( 0.0, 0.0, -1.0 ),
  float3( 0.688189, 0.525736, 0.499997 ),
  float3( -0.262869, 0.525738, 0.809012 ),
  float3( -0.850648, 0.525736, 0.0 ),
  float3( -0.262869, 0.525738, -0.809012 ),
  float3( 0.688189, 0.525736, -0.499997 ),
  float3( 0.162456, 0.850654, 0.499995 ),
  float3( 0.52573, 0.850652, 0.0 ),
  float3( -0.425323, 0.850654, 0.309011 ),
  float3( -0.425323, 0.850654, -0.309011 ),
  float3( 0.162456, 0.850654, -0.499995 ),
};

static const uint3 kIndices[] = {
  uint3( 1, 14, 13 ),  uint3( 2, 14, 16 ),  uint3( 1, 13, 18 ),  uint3( 1, 18, 20 ),  uint3( 1, 20, 17 ),
  uint3( 2, 16, 23 ),  uint3( 3, 15, 25 ),  uint3( 4, 19, 27 ),  uint3( 5, 21, 29 ),  uint3( 6, 22, 31 ),
  uint3( 2, 23, 26 ),  uint3( 3, 25, 28 ),  uint3( 4, 27, 30 ),  uint3( 5, 29, 32 ),  uint3( 6, 31, 24 ),
  uint3( 7, 33, 38 ),  uint3( 8, 34, 40 ),  uint3( 9, 35, 41 ),  uint3( 10, 36, 42 ), uint3( 11, 37, 39 ),
  uint3( 39, 42, 12 ), uint3( 39, 37, 42 ), uint3( 37, 10, 42 ), uint3( 42, 41, 12 ), uint3( 42, 36, 41 ),
  uint3( 36, 9, 41 ),  uint3( 41, 40, 12 ), uint3( 41, 35, 40 ), uint3( 35, 8, 40 ),  uint3( 40, 38, 12 ),
  uint3( 40, 34, 38 ), uint3( 34, 7, 38 ),  uint3( 38, 39, 12 ), uint3( 38, 33, 39 ), uint3( 33, 11, 39 ),
  uint3( 24, 37, 11 ), uint3( 24, 31, 37 ), uint3( 31, 10, 37 ), uint3( 32, 36, 10 ), uint3( 32, 29, 36 ),
  uint3( 29, 9, 36 ),  uint3( 30, 35, 9 ),  uint3( 30, 27, 35 ), uint3( 27, 8, 35 ),  uint3( 28, 34, 8 ),
  uint3( 28, 25, 34 ), uint3( 25, 7, 34 ),  uint3( 26, 33, 7 ),  uint3( 26, 23, 33 ), uint3( 23, 11, 33 ),
  uint3( 31, 32, 10 ), uint3( 31, 22, 32 ), uint3( 22, 5, 32 ),  uint3( 29, 30, 9 ),  uint3( 29, 21, 30 ),
  uint3( 21, 4, 30 ),  uint3( 27, 28, 8 ),  uint3( 27, 19, 28 ), uint3( 19, 3, 28 ),  uint3( 25, 26, 7 ),
  uint3( 25, 15, 26 ), uint3( 15, 2, 26 ),  uint3( 23, 24, 11 ), uint3( 23, 16, 24 ), uint3( 16, 6, 24 ),
  uint3( 17, 22, 6 ),  uint3( 17, 20, 22 ), uint3( 20, 5, 22 ),  uint3( 20, 21, 5 ),  uint3( 20, 18, 21 ),
  uint3( 18, 4, 21 ),  uint3( 18, 19, 4 ),  uint3( 18, 13, 19 ), uint3( 13, 3, 19 ),  uint3( 16, 17, 6 ),
  uint3( 16, 14, 17 ), uint3( 14, 1, 17 ),  uint3( 13, 15, 3 ),  uint3( 13, 14, 15 ), uint3( 14, 2, 15 ),
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
    out vertices MSVertexOut      verts[41],
    out indices uint3             tris[80],
    out primitives MSPrimitiveOut out_light_id[80] )
{
  StructuredBuffer<PointLight> point_lights = ResourceDescriptorHeap[g_PointLights];
  ConstantBuffer<Camera>       camera       = ResourceDescriptorHeap[g_Camera];

  uint                         light_idx    = lights.LightID[IN.GroupID.x];

  SetMeshOutputCounts( 41, 80 );

  for ( int i = IN.LocalID.x; i < 41; i += 32 )
  {
    // Correct it so unit sphere is inside the light volume
    float4 local = float4(
        normalize( kVertices[i] ) * 1.06f * point_lights[light_idx].Range + point_lights[light_idx].Position, 1.0f );
    float4 screen_pos       = mul( camera.Projection, mul( camera.View, local ) );
    verts[i].ScreenPosition = screen_pos;
    verts[i].TexCoord       = screen_pos.xy / screen_pos.w * float2( 0.5f, -0.5f ) + 0.5f;
  }

  for ( int i = IN.LocalID.x; i < 80; i += 32 )
  {
    out_light_id[i].LightID = light_idx;
    tris[i]                 = kIndices[i] - uint3( 1, 1, 1 );
  }
}
