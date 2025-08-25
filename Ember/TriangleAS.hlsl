#include "Triangle.hlsli"
#include "Utility.hlsli"

NUM_THREADS( 1, 1, 1 )
void TriangleAS( uint3 dt_id : SV_DispatchThreadID )
{
  uint           mesh_draw_idx = dt_id.x;
  uint           meshlet_count = 0;
  MeshletPayload pl;

  if ( mesh_draw_idx < g_DrawList.MeshDrawCount )
  {
    StructuredBuffer<MeshDraw> mesh_draws   = ResourceDescriptorHeap[g_DrawList.MeshDraws];
    MeshDraw                   current_draw = mesh_draws[NonUniformResourceIndex( mesh_draw_idx )];

    meshlet_count                           = current_draw.MeshletCount;

    pl.Transform                            = current_draw.FirstTransform;
    pl.FirstVertex                          = current_draw.FirstVertex;
    pl.FirstMeshlet                         = current_draw.FirstMeshlet;
    pl.MeshletBuffer                        = current_draw.MeshletBuffer;
    pl.MeshletTriangleBuffer                = current_draw.MeshletTriangleBuffer;
    pl.MeshletIndexBuffer                   = current_draw.MeshletIndexBuffer;
    pl.VertexBuffer                         = current_draw.VertexBuffer;
    pl.Material                             = current_draw.Material;
  }

  DispatchMesh( meshlet_count, 1u, 1u, pl );
}
