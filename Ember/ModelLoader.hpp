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

struct VertexPosition
{
  DirectX::XMFLOAT3                         Position;

  constexpr static D3D12_INPUT_ELEMENT_DESC kInputElementDesc[] = {
    PerVertexInput( "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0 ),
  };
};

struct VertexData
{
  DirectX::XMFLOAT3                         Normal;
  DirectX::XMFLOAT4                         Tangent;
  DirectX::XMFLOAT3                         Color;
  DirectX::XMFLOAT2                         TexCoord0;
  DirectX::XMFLOAT2                         TexCoord1;

  constexpr static D3D12_INPUT_ELEMENT_DESC kInputElementDesc[] = {
    PerVertexInput( "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 1 ),
    PerVertexInput( "TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1 ),
    PerVertexInput( "COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 1 ),
    PerVertexInput( "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 1 ),
    PerVertexInput( "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 1 ),
  };
};

class ModelLoader
{
  struct LoadingContext
  {
    std::map<cgltf_material const*, Material*> MaterialCache;
    Geometry*                                  Geometry;
    std::vector<VertexPosition>                VertexPositions;
    std::vector<VertexData>                    VertexData;
    std::vector<uint32_t>                      Indices;
  };

  RenderDevice*  m_RenderDevice;
  World*         m_World;
  TextureLoader* m_TextureLoader;

  flecs::entity  ProcessNode( LoadingContext* context, flecs::entity parent, cgltf_node const& node );
  void           ProcessMesh( LoadingContext* context, flecs::entity owning, cgltf_mesh const& mesh ) const;
  bool      TryLoadTexture( Texture* texture, cgltf_image const& image, ColorSpaceOverride color_space_override ) const;
  Material* TryProcessMaterial( LoadingContext* context, cgltf_material const* material ) const;

public:
  std::optional<flecs::entity> TryLoadModel( char const* filename );

  ModelLoader( RenderDevice* render_device, World* world, TextureLoader* texture_loader );
};

} // namespace Ember
