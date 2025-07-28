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

struct Vertex
{
  DirectX::XMFLOAT3 Position;
  DirectX::XMFLOAT3 Color;
  DirectX::XMFLOAT2 TexCoord0;
};

class ModelLoader
{
  RenderDevice* m_RenderDevice;
  World*        m_World;
  TextureLoader m_TextureLoader;

  void          ProcessNode(
               Model*                 model,
               Node*                  parent,
               MeshData*              mesh_data,
               std::vector<Vertex>*   vertices,
               std::vector<uint16_t>* indices,
               cgltf_node const&      node );
  void ProcessMesh(
      Model*                 model,
      Node*                  parent,
      MeshData*              mesh_data,
      std::vector<Vertex>*   vertices,
      std::vector<uint16_t>* indices,
      cgltf_mesh const&      mesh );

  bool TryLoadTexture(
      Texture* texture, cgltf_image const& image, TextureLoader::ColorSpaceOverride color_space_override );
  Material* TryProcessMaterial( Model* model, cgltf_material const& material );

public:
  Model* LoadModel( char const* filename );
  void   Update();
  void   FlushBarriers( ID3D12GraphicsCommandList* command_list );

  ModelLoader( RenderDevice* render_device, World* world );
};


} // namespace Ember
