#pragma once

#include <cgltf.h>
#include <map>

#include <Util/DirectXHeaders.hpp>
#include <Util/Float16.hpp>
#include "Color.hpp"
#include "Scene.hpp"
#include "TextureLoader.hpp"

#include <expected>

namespace Ember
{
class RenderDevice;

constexpr D3D12_INPUT_ELEMENT_DESC PerVertexInput(
    char const* name, uint32_t const index, DXGI_FORMAT const format, uint32_t const input_slot )
{
  return {
    .SemanticName         = name,
    .SemanticIndex        = index,
    .Format               = format,
    .InputSlot            = input_slot,
    .AlignedByteOffset    = D3D12_APPEND_ALIGNED_ELEMENT,
    .InputSlotClass       = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
    .InstanceDataStepRate = 0,
  };
}

struct VertexLite
{
  Float16                                   PositionX;  // 02
  Float16                                   PositionY;  // 04
  Float16                                   PositionZ;  // 06
  Float16                                   PositionW;  // 08
  Float16                                   TexCoord0X; // 10
  Float16                                   TexCoord0Y; // 12
  Float16                                   TexCoord1X; // 14
  Float16                                   TexCoord1Y; // 16

  constexpr static D3D12_INPUT_ELEMENT_DESC kInputElementDesc[] = {
    PerVertexInput( "POSITION", 0, DXGI_FORMAT_R16G16B16A16_FLOAT, 0 ),
    PerVertexInput( "TEXCOORD", 0, DXGI_FORMAT_R16G16_FLOAT, 0 ),
    PerVertexInput( "TEXCOORD", 1, DXGI_FORMAT_R16G16_FLOAT, 0 ),
  };
};

struct alignas( 16 ) VertexData
{
  uint32_t                                  QuantizedNormal;  // 04
  uint32_t                                  QuantizedTangent; // 08
  Color32                                   Color;            // 12
  uint32_t                                  Padding0;         // 16

  constexpr static D3D12_INPUT_ELEMENT_DESC kInputElementDesc[] = {
    PerVertexInput( "NORMAL", 0, DXGI_FORMAT_R10G10B10A2_UNORM, 0 ),
    PerVertexInput( "TANGENT", 0, DXGI_FORMAT_R10G10B10A2_UNORM, 0 ),
    PerVertexInput( "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0 ),
  };
};

struct Meshlet
{
  uint32_t VertexOffset;   // 04 04
  uint32_t TriangleOffset; // 04 08
  uint32_t VertexCount;    // 04 12
  uint32_t TriangleCount;  // 04 16
  Float16  CenterX;        // 02 18
  Float16  CenterY;        // 02 20
  Float16  CenterZ;        // 02 22
  Float16  Radius;         // 02 24
  uint32_t ConeInfo;       // 04 28
  uint32_t ConeApexOffset; // 04 32
};

class ModelLoader
{
  size_t constexpr static kMaxVertices  = 64;
  size_t constexpr static kMaxTriangles = 124;
  float constexpr static kConeWeight    = 0.0f;

  struct LoadingContext
  {
    struct Offsets
    {
      size_t VertexPositions;
      size_t VertexData;
      size_t Meshlets;
      size_t MeshletTriangles;
      size_t MeshletVertices;
      size_t Indices;
    };
    std::map<cgltf_material const*, MaterialImpl*> MaterialCache;
    std::map<cgltf_node const*, flecs::entity>     NodeCache;
    GeometryImpl*                                  Geometry;
    std::vector<Meshlet>                           Meshlets;
    std::vector<uint32_t>                          MeshletVertices;
    std::vector<byte>                              MeshletTriangles;
    std::vector<VertexLite>                        VertexPositions;
    std::vector<VertexData>                        VertexData;
    std::vector<uint32_t>                          Indices;
    Offsets                                        Offsets;
    CommandList                                    CommandList;
  };

  RenderDevice*          m_RenderDevice;
  World*                 m_World;
  std::shared_ptr<Queue> m_ComputeContext;
  TextureLoader*         m_TextureLoader;
  MaterialManager*       m_MaterialManager;
  GeometryManager*       m_GeometryManager;

  void                   ProcessNode( LoadingContext* context, flecs::entity parent, cgltf_node const& node ) const;
  void ProcessPrimitive( LoadingContext* context, flecs::entity owning, cgltf_primitive const& primitive ) const;
  void ProcessMesh( LoadingContext* context, flecs::entity owning, cgltf_mesh const& mesh ) const;
  bool TryLoadTexture( Texture* texture, cgltf_image const& image, ColorSpaceOverride color_space_override ) const;
  [[nodiscard]] MaterialImpl* TryProcessMaterial( LoadingContext* context, cgltf_material const* material ) const;
  [[nodiscard]] MaterialImpl* GetDefaultMaterial( LoadingContext* context ) const;
  void                        ProcessAnimation( LoadingContext* context, cgltf_animation const& animation ) const;
  void                        FinalizeGeometry( LoadingContext* context, flecs::entity entity ) const;
  void                        CreateAccelerationStructure( LoadingContext* context, flecs::entity root ) const;

  [[nodiscard]] BottomLevelAS CreateBottomLevelAS(
      LoadingContext*           context,
      D3D12_GPU_VIRTUAL_ADDRESS index_addr,
      D3D12_GPU_VIRTUAL_ADDRESS vert_addr,
      uint32_t                  index_count,
      uint32_t                  vertex_count ) const;

public:
  enum class Error : uint8_t
  {
    kCannotOpenFile,
    kInvalidFile,
    kCannotLoadMemory,
  };

  std::expected<flecs::entity, Error> TryLoadModel( char const* filename );

  ModelLoader(
      RenderDevice*          render_device,
      World*                 world,
      std::shared_ptr<Queue> compute_context,
      TextureLoader*         texture_loader,
      MaterialManager*       material_manager,
      GeometryManager*       geometry_manager );
};

} // namespace Ember
