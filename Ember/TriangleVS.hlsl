#include "Triangle.hlsli"

VSOut TriangleVS( uint vertex_idx : SV_VERTEXID )
{
  VSOut                        OUT;

  ConstantBuffer<Camera>       camera        = ResourceDescriptorHeap[g_Camera];

  StructuredBuffer<VertexData> vertex_buffer = ResourceDescriptorHeap[g_VertexBufferIdx];
  VertexData                   vertex        = vertex_buffer[NonUniformResourceIndex( vertex_idx + g_FirstVertex )];

  float4                       world_pos     = mul( g_Model, vertex.GetPosition() );
  float4                       clip_pos      = mul( camera.View, world_pos );
  float4                       screen_pos    = mul( camera.Projection, clip_pos );

  float3                       normal        = normalize( mul( vertex.GetNormal(), g_InvModel ).xyz );
  float4                       tangent       = vertex.GetTangent();
  tangent            = float4( normalize( mul( float4( tangent.xyz, 0.0f ), g_InvModel ).xyz ), tangent.w );

  OUT.ScreenPosition = screen_pos;
  OUT.Position       = world_pos;
  OUT.Normal         = normal;
  OUT.LinearDepth    = clip_pos.z;
  OUT.Tangent        = tangent;
  OUT.Color          = vertex.GetColor();
  OUT.TexCoord[0]    = vertex.GetTexCoord( 0 );
  OUT.TexCoord[1]    = vertex.GetTexCoord( 1 );
  return OUT;
}
