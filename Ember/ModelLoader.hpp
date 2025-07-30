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
  DirectX::XMFLOAT3 Normal;
  DirectX::XMFLOAT4 Tangent;
  DirectX::XMFLOAT3 Color;
  DirectX::XMFLOAT2 TexCoord0;
  DirectX::XMFLOAT2 TexCoord1;
};

class ModelLoader
{
  struct LoadingContext
  {
    Model*                 Model;
    MeshData*              MeshData;
    std::vector<Vertex>*   Vertices;
    std::vector<uint16_t>* Indices;
  };

  RenderDevice* m_RenderDevice;
  World*        m_World;
  TextureLoader m_TextureLoader;

  void          ProcessNode( LoadingContext* context, Node* parent, cgltf_node const& node );
  void          ProcessMesh( LoadingContext* context, Node* parent, cgltf_mesh const& mesh );
  bool          TryLoadTexture( Texture* texture, cgltf_image const& image, ColorSpaceOverride color_space_override );
  Material*     TryProcessMaterial( Model* model, cgltf_material const& material );

public:
  Model* TryLoadModel( char const* filename );
  void   Update();
  void   FlushBarriers( ID3D12GraphicsCommandList* command_list );

  ModelLoader( RenderDevice* render_device, World* world );
};


} // namespace Ember
