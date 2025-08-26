#pragma once

#include <cgltf.h>
#include <map>

#include "Scene.hpp"
#include "TextureLoader.hpp"
#include "Util/DirectXHeaders.hpp"

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

struct ShadowVertex
{
  uint16_t                                  Px;
  uint16_t                                  Py;
  uint16_t                                  Pz;
  uint16_t                                  Pw;

  constexpr static D3D12_INPUT_ELEMENT_DESC kInputElementDesc[] = {
    PerVertexInput( "POSITION", 0, DXGI_FORMAT_R16G16B16A16_FLOAT, 0 ),
  };
};

struct alignas( 16 ) VertexData
{
  uint16_t                                  PositionX;        // 02
  uint16_t                                  PositionY;        // 04
  uint16_t                                  PositionZ;        // 06
  uint16_t                                  PositionW;        // 08
  uint32_t                                  QuantizedNormal;  // 12
  uint32_t                                  QuantizedTangent; // 16
  Color32                                   Color;            // 20
  uint16_t                                  TexCoord0X;       // 22
  uint16_t                                  TexCoord0Y;       // 24
  uint16_t                                  TexCoord1X;       // 26
  uint16_t                                  TexCoord1Y;       // 28
  uint32_t                                  Padding0;         // 32

  constexpr static D3D12_INPUT_ELEMENT_DESC kInputElementDesc[] = {
    PerVertexInput( "POSITION", 0, DXGI_FORMAT_R16G16B16A16_FLOAT, 0 ),
    PerVertexInput( "NORMAL", 0, DXGI_FORMAT_R10G10B10A2_UNORM, 0 ),
    PerVertexInput( "TANGENT", 0, DXGI_FORMAT_R10G10B10A2_UNORM, 0 ),
    PerVertexInput( "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0 ),
    PerVertexInput( "TEXCOORD", 0, DXGI_FORMAT_R16G16_FLOAT, 0 ),
    PerVertexInput( "TEXCOORD", 1, DXGI_FORMAT_R16G16_FLOAT, 0 ),
  };
};

struct Meshlet
{
  uint32_t VertexOffset;
  uint32_t TriangleOffset;
  uint32_t VertexCount;
  uint32_t TriangleCount;
  uint16_t CenterX;
  uint16_t CenterY;
  uint16_t CenterZ;
  uint16_t Radius;
};

class ModelLoader
{
  size_t constexpr static kMaxVertices  = 64;
  size_t constexpr static kMaxTriangles = 124;
  float constexpr static kConeWeight    = 0.0f;

  struct LoadingContext
  {
    std::map<cgltf_material const*, MaterialImpl*> MaterialCache;
    GeometryImpl*                                  Geometry;
    std::vector<Meshlet>                           Meshlets;
    std::vector<uint32_t>                          MeshletVertices;
    std::vector<byte>                              MeshletTriangles;
    std::vector<ShadowVertex>                      VertexPositions;
    std::vector<VertexData>                        VertexData;
    std::vector<uint32_t>                          Indices;
  };

  RenderDevice*    m_RenderDevice;
  World*           m_World;
  TextureLoader*   m_TextureLoader;
  MaterialManager* m_MaterialManager;

  flecs::entity    ProcessNode( LoadingContext* context, flecs::entity parent, cgltf_node const& node );
  void ProcessPrimitive( LoadingContext* context, flecs::entity owning, cgltf_primitive const& primitive ) const;
  void ProcessMesh( LoadingContext* context, flecs::entity owning, cgltf_mesh const& mesh ) const;
  bool TryLoadTexture( Texture* texture, cgltf_image const& image, ColorSpaceOverride color_space_override ) const;
  MaterialImpl* TryProcessMaterial( LoadingContext* context, cgltf_material const* material ) const;
  MaterialImpl* GetDefaultMaterial( LoadingContext* context ) const;

public:
  std::optional<flecs::entity> TryLoadModel( char const* filename );

  ModelLoader(
      RenderDevice* render_device, World* world, TextureLoader* texture_loader, MaterialManager* material_manager );
};

} // namespace Ember
