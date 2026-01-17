#pragma once

#include <Graphics/Buffer.hpp>
#include <Util/DirectXHeaders.hpp>
#include "Material.hpp"

namespace Ember
{
struct BottomLevelAS;
struct WorldTransform;
struct Mesh;
class CommandList;
class Material;
class GeometryManager;
class RenderDevice;

// Contains all the information for a single 'mesh'
// TODO: Let this be resident in the VRAM
struct DrawMesh
{
  uint32_t       VertexDataStart; // 04 04
  uint32_t       VertexLiteStart; // 04 08
  MaterialHandle Material;        // 04 12
  uint32_t       FirstMeshlet;    // 04 16
  uint32_t       IndexStart;      // 04 20
};

// What transform, which mesh
// Should be updated every frame. (For dynamic)
struct DrawInstance
{
  DirectX::XMFLOAT4X4 Transform;    // 64  64
  DirectX::XMFLOAT4X4 InvTransform; // 64 128
  uint32_t            MeshID;       // 04 132 // TODO: Tuck this into the matrices.
};

/*
 * Which instance to pick up (for the amplification shader)
 * Update every frame
 *
 * TODO: Split into commands with exactly 32 meshlets.
 * Bucket the rest into a special command set.
 */
struct AmpCommand
{
  uint32_t InstanceID;   // Which instance (index DrawInstance)
  uint32_t FirstMeshlet; // Which meshlet of this instance. (Mesh.FirstMeshlet + FirstMeshlet in the geometry)
  uint32_t MeshletCount; // How many meshlets to draw.
  uint32_t Pad0;
};

class DrawList
{
  struct FrameResources
  {
    Buffer RaytracingInstances;
    Buffer RaytracingScratch;
    Buffer TopLevelAS;
    Buffer UnifiedResourceBuffer;
  };

  using RaytracingInstance = D3D12_RAYTRACING_INSTANCE_DESC;

  RenderDevice*               m_RenderDevice;
  GeometryManager*            m_GeometryManager;
  MaterialManager*            m_MaterialManager;

  std::vector<FrameResources> m_FrameResources;

  // New API
  std::vector<RaytracingInstance> m_RaytracingInstances;
  std::vector<DrawMesh>           m_Meshes;
  std::vector<DrawInstance>       m_Instances;
  std::vector<AmpCommand>         m_OpaqueCommands;
  std::vector<AmpCommand>         m_MaskedCommands;
  std::vector<AmpCommand>         m_TransparentCommands;

public:
  struct PerBatch
  {
    SRVHandle GeometryBuffer;
    SRVHandle MaterialBuffer;
    SRVHandle TopLevelAS;
    SRVHandle DrawBuffer;
    uint32_t  InstancesOffset;
    uint32_t  CommandsOffset;
    uint32_t  CommandsCount;
  };

  struct Batches
  {
    SRVHandle              GeometryBuffer;
    SRVHandle              MaterialBuffer;
    SRVHandle              TopLevelAS;
    SRVHandle              DrawBuffer;
    uint32_t               InstancesOffset;
    uint32_t               OpaqueCommandsOffset;
    uint32_t               MaskedCommandsOffset;
    uint32_t               TransparentCommandsOffset;
    uint32_t               CommandsEnd;

    [[nodiscard]] PerBatch Opaque() const;
    [[nodiscard]] PerBatch Masked() const;
    [[nodiscard]] PerBatch Transparent() const;

  private:
    // Helpers
    [[nodiscard]] uint32_t OpaqueCommandsCount() const;
    [[nodiscard]] uint32_t MaskedCommandsCount() const;
    [[nodiscard]] uint32_t TransparentCommandsCount() const;
  };

  DrawList(
      RenderDevice*    render_device,
      GeometryManager* geometry_manager,
      MaterialManager* material_manager,
      uint32_t         frame_count );

  void PushDraw(
      WorldTransform const& transform, Mesh const& mesh, Material const& material, BottomLevelAS const& blas );
  [[nodiscard]] Batches PrepareFrame( uint32_t frame_idx );
  [[nodiscard]] Batches PrepareFrameWithRaytracing( CommandList* cmd, uint32_t frame_idx );
  void                  Clear();
  [[nodiscard]] size_t  GetOpaqueCount() const;
  [[nodiscard]] size_t  GetMaskedCount() const;
  [[nodiscard]] size_t  GetTransparentCount() const;
  [[nodiscard]] size_t  GetTotalCount() const;
};

} // namespace Ember
