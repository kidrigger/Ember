#pragma once

#include <cgltf.h>

#include "Scene.hpp"
#include "TextureLoader.hpp"
#include "Util/DirectXHeaders.hpp"

namespace Ember
{
class World;
class Model;
class RenderDevice;

constexpr D3D12_INPUT_ELEMENT_DESC PerVertexInput( char const* name, uint32_t const index, DXGI_FORMAT const format )
{
  return {
    .SemanticName         = name,
    .SemanticIndex        = index,
    .Format               = format,
    .InputSlot            = 0,
    .AlignedByteOffset    = D3D12_APPEND_ALIGNED_ELEMENT,
    .InputSlotClass       = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
    .InstanceDataStepRate = 0,
  };
}

struct Vertex
{
  DirectX::XMFLOAT3                         Position;
  DirectX::XMFLOAT3                         Normal;
  DirectX::XMFLOAT4                         Tangent;
  DirectX::XMFLOAT3                         Color;
  DirectX::XMFLOAT2                         TexCoord0;
  DirectX::XMFLOAT2                         TexCoord1;

  constexpr static D3D12_INPUT_ELEMENT_DESC kInputElementDesc[] = {
    PerVertexInput( "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT ),
    PerVertexInput( "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT ),
    PerVertexInput( "TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT ),
    PerVertexInput( "COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT ),
    PerVertexInput( "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT ),
    PerVertexInput( "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT ),
  };
};

class ModelLoader
{
  struct LoadingContext
  {
    Model*                 Model;
    MeshData*              MeshData;
    std::vector<Vertex>*   Vertices;
    std::vector<uint32_t>* Indices;
  };

  RenderDevice*  m_RenderDevice;
  World*         m_World;
  TextureLoader* m_TextureLoader;

  void           ProcessNode( LoadingContext* context, Node* parent, cgltf_node const& node );
  void           ProcessMesh( LoadingContext* context, Node* parent, cgltf_mesh const& mesh ) const;
  bool      TryLoadTexture( Texture* texture, cgltf_image const& image, ColorSpaceOverride color_space_override ) const;
  Material* TryProcessMaterial( Model* model, cgltf_material const& material ) const;

public:
  Model* TryLoadModel( char const* filename );
  void   Update();
  void   FlushBarriers( ID3D12GraphicsCommandList* command_list );

  ModelLoader( RenderDevice* render_device, World* world, TextureLoader* texture_loader );
};

} // namespace Ember
