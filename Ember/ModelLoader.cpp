#include "ModelLoader.hpp"

#include "BasicApp.hpp"
#include "RenderDevice.hpp"
#include "Util/DataUtil.hpp"
#include "Util/HelperUtils.hpp"

void LoadAttribute(
    std::vector<Ember::Vertex>* vertices,
    int32_t const               vertex_start,
    std::vector<float>*         scratch,
    cgltf_attribute const&      position_attr,
    size_t const                stride,
    size_t const                offset,
    size_t const                components )
{
  size_t const float_count = cgltf_accessor_unpack_floats( position_attr.data, nullptr, 0 );
  ASSERT( float_count % components == 0 );
  scratch->resize( float_count );
  cgltf_accessor_unpack_floats( position_attr.data, scratch->data(), scratch->size() );

  // Guaranteed to have space for these vertices.
  vertices->resize( vertex_start + float_count / components );

  byte*        write_ptr = reinterpret_cast<byte*>( vertices->data() + vertex_start ) + offset;
  float const* read_ptr  = scratch->data();
  for ( size_t i = vertex_start; i < vertices->size(); ++i )
  {
    memcpy( write_ptr, read_ptr, components * sizeof( float ) );

    read_ptr  += components;
    write_ptr += stride;
  }

  scratch->clear();
}

void Ember::ModelLoader::ProcessMesh( LoadingContext* context, Node* parent, cgltf_mesh const& mesh )
{
  using namespace std::string_view_literals;

  auto [model, mesh_data, vertices, indices] = *context;

  cgltf_primitive const* primitives          = mesh.primitives;

  std::vector<Primitive> primitive_acc;
  for ( uint32_t primitive_index = 0; primitive_index < mesh.primitives_count; ++primitive_index )
  {
    // VertexStart is per-primitive
    int32_t const          vertex_start = static_cast<int32_t>( vertices->size() );

    cgltf_primitive const& primitive    = primitives[primitive_index];

    ASSERT( primitive.type == cgltf_primitive_type_triangles );

    // Index Buffer
    ASSERT(
        primitive.indices->type == cgltf_type_scalar and
        ( primitives->indices->component_type == cgltf_component_type_r_16u or
          primitives->indices->component_type == cgltf_component_type_r_8u ) );
    size_t const index_start = indices->size();
    size_t const index_count = cgltf_accessor_unpack_indices( primitive.indices, nullptr, sizeof indices->at( 0 ), 0 );
    ASSERT( index_count > 0 );
    indices->resize( index_start + index_count );
    cgltf_accessor_unpack_indices(
        primitive.indices, indices->data() + index_start, sizeof indices->at( 0 ), index_count );

    // Material

    Material* material = nullptr;
    if ( primitive.material )
    {
      material = TryProcessMaterial( model, *primitive.material );
    }

    primitive_acc.push_back( {
        .Material = material,
        .DrawInfo = {
                     .FirstIndex  = ( uint32_t )index_start,
                     .IndexCount  = ( uint32_t )index_count,
                     .FirstVertex = ( uint32_t )vertex_start,
                     }
    } );

    std::vector<float>     scratch;

    cgltf_attribute const* attributes = primitive.attributes;
    for ( uint32_t attrib_index = 0; attrib_index < primitive.attributes_count; ++attrib_index )
    {
      if ( "POSITION"sv == attributes[attrib_index].name )
      {
        cgltf_attribute const& position_attr = attributes[attrib_index];
        ASSERT( position_attr.data->component_type == cgltf_component_type_r_32f );
        ASSERT( position_attr.data->type == cgltf_type_vec3 );

        size_t constexpr stride     = sizeof( Vertex );
        size_t constexpr offset     = offsetof( Vertex, Position );
        size_t constexpr components = 3;

        LoadAttribute( vertices, vertex_start, &scratch, position_attr, stride, offset, components );
      }
      if ( "NORMAL"sv == attributes[attrib_index].name )
      {
        cgltf_attribute const& normal_attr = attributes[attrib_index];
        ASSERT( normal_attr.data->component_type == cgltf_component_type_r_32f );
        ASSERT( normal_attr.data->type == cgltf_type_vec3 );

        size_t constexpr stride     = sizeof( Vertex );
        size_t constexpr offset     = offsetof( Vertex, Normal );
        size_t constexpr components = 3;

        LoadAttribute( vertices, vertex_start, &scratch, normal_attr, stride, offset, components );
      }
      if ( "TANGENT"sv == attributes[attrib_index].name )
      {
        cgltf_attribute const& tangent_attr = attributes[attrib_index];
        ASSERT( tangent_attr.data->component_type == cgltf_component_type_r_32f );
        ASSERT( tangent_attr.data->type == cgltf_type_vec4 );

        size_t constexpr stride     = sizeof( Vertex );
        size_t constexpr offset     = offsetof( Vertex, Tangent );
        size_t constexpr components = 4;

        LoadAttribute( vertices, vertex_start, &scratch, tangent_attr, stride, offset, components );
      }
      if ( "TEXCOORD_0"sv == attributes[attrib_index].name )
      {
        cgltf_attribute const& tex_coord_attr = attributes[attrib_index];
        ASSERT( tex_coord_attr.data->component_type == cgltf_component_type_r_32f );
        ASSERT( tex_coord_attr.data->type == cgltf_type_vec2 );

        size_t constexpr stride     = sizeof( Vertex );
        size_t constexpr offset     = offsetof( Vertex, TexCoord0 );
        size_t constexpr components = 2;

        LoadAttribute( vertices, vertex_start, &scratch, tex_coord_attr, stride, offset, components );
      }
      if ( "TEXCOORD_1"sv == attributes[attrib_index].name )
      {
        cgltf_attribute const& tex_coord_attr = attributes[attrib_index];
        ASSERT( tex_coord_attr.data->component_type == cgltf_component_type_r_32f );
        ASSERT( tex_coord_attr.data->type == cgltf_type_vec2 );

        size_t constexpr stride     = sizeof( Vertex );
        size_t constexpr offset     = offsetof( Vertex, TexCoord1 );
        size_t constexpr components = 2;

        LoadAttribute( vertices, vertex_start, &scratch, tex_coord_attr, stride, offset, components );
      }
      if ( "COLOR_0"sv == attributes[attrib_index].name )
      {
        cgltf_attribute const& color_attr = attributes[attrib_index];
        ASSERT( color_attr.data->component_type == cgltf_component_type_r_32f );

        size_t constexpr stride = sizeof( Vertex );
        size_t constexpr offset = offsetof( Vertex, Color );
        size_t components       = 3;
        switch ( color_attr.data->type )
        {
          case cgltf_type_vec3:
            components = 3;
            break;
          case cgltf_type_vec4:
            components = 4;
            break;
          default:
            UNREACHABLE;
        }

        LoadAttribute( vertices, vertex_start, &scratch, color_attr, stride, offset, components );
      }
      // TODO: Grab other attributes.
    }
  }

  parent->CreateChildObject<Mesh>( mesh_data, primitive_acc );
}

bool Ember::ModelLoader::TryLoadTexture(
    Texture* texture, cgltf_image const& image, ColorSpaceOverride const color_space_override )
{
  byte* data;
  if ( image.buffer_view->data )
  {
    data = static_cast<byte*>( image.buffer_view->data );
  }
  else
  {
    data = static_cast<byte*>( image.buffer_view->buffer->data ) + image.buffer_view->offset;
  }
  size_t const size = image.buffer_view->size;

  std::string  id   = image.name ? image.name : ( image.uri ? image.uri : "" );
  if ( id.empty() )
  {
    uint64_t const hash  = HashFnv1A( size, data );
    id                  += "DataHash:";
    id                  += std::to_string( hash );
  }

  m_TextureLoader->TryLoadTextureFromData( texture, id.c_str(), size, data, color_space_override );

  return texture;
}

void Ember::ModelLoader::ProcessNode( LoadingContext* context, Node* parent, cgltf_node const& node )
{
  Node* my_node = parent->CreateChildObject<Node>();

  if ( node.has_matrix )
  {
    my_node->SetLocalTransform( DirectX::XMMATRIX{ node.matrix } );
  }
  else
  {
    if ( node.has_translation )
      my_node->SetLocalTranslation(
          DirectX::XMVectorSet( node.translation[0], node.translation[1], node.translation[2], 1.0f ) );
    if ( node.has_rotation )
      my_node->SetLocalRotation(
          DirectX::XMVectorSet( node.rotation[0], node.rotation[1], node.rotation[2], node.rotation[3] ) );
    if ( node.has_scale )
      my_node->SetLocalScale( DirectX::XMVectorSet( node.scale[0], node.scale[1], node.scale[2], 1.0f ) );
  }

  if ( node.mesh )
  {
    ProcessMesh( context, my_node, *node.mesh );
  }

  for ( uint32_t child_idx = 0; child_idx < node.children_count; ++child_idx )
  {
    ProcessNode( context, my_node, *node.children[child_idx] );
  }
}

Ember::Material* Ember::ModelLoader::TryProcessMaterial( Model* model, cgltf_material const& material )
{
  ASSERT( material.has_pbr_metallic_roughness );

  auto const base_color_factor = DirectX::XMFLOAT4{ material.pbr_metallic_roughness.base_color_factor };
  auto const emissive_factor   = DirectX::XMFLOAT4{
    material.emissive_factor[0],
    material.emissive_factor[1],
    material.emissive_factor[2],
    0.0f,
  };
  auto const emissive_strength = std::max( material.emissive_strength.emissive_strength, 1.0f );

  Texture    base_color_texture;
  Texture    normal_texture;
  Texture    metal_rough_texture;
  Texture    emissive_texture;

  D3D12_SAMPLER_DESC constexpr sampler_desc = {
    .Filter         = D3D12_FILTER_ANISOTROPIC,
    .AddressU       = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
    .AddressV       = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
    .AddressW       = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
    .MipLODBias     = 0.0f,
    .MaxAnisotropy  = D3D12_DEFAULT_MAX_ANISOTROPY,
    .ComparisonFunc = D3D12_COMPARISON_FUNC_NONE,
    .BorderColor    = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK,
    .MinLOD         = 0.0f,
    .MaxLOD         = INFINITY,
  };

  Sampler sampler = m_RenderDevice->CreateSampler( sampler_desc );

  if ( material.pbr_metallic_roughness.base_color_texture.texture )
  {
    cgltf_image const* base_color_image = material.pbr_metallic_roughness.base_color_texture.texture->image;

    if ( not TryLoadTexture( &base_color_texture, *base_color_image, ColorSpaceOverride::kSrgb ) )
    {
      return nullptr;
    }
  }

  if ( material.pbr_metallic_roughness.metallic_roughness_texture.texture )
  {
    cgltf_image const* metal_rough_image = material.pbr_metallic_roughness.metallic_roughness_texture.texture->image;

    if ( not TryLoadTexture( &metal_rough_texture, *metal_rough_image, ColorSpaceOverride::kLinear ) )
    {
      return nullptr;
    }
  }

  if ( material.normal_texture.texture )
  {
    cgltf_image const* normal_image = material.normal_texture.texture->image;

    if ( not TryLoadTexture( &normal_texture, *normal_image, ColorSpaceOverride::kLinear ) )
    {
      return nullptr;
    }
  }

  if ( material.emissive_texture.texture )
  {
    cgltf_image const* emissive_image = material.emissive_texture.texture->image;

    if ( not TryLoadTexture( &emissive_texture, *emissive_image, ColorSpaceOverride::kSrgb ) )
    {
      return nullptr;
    }
  }

  float const metallic     = material.pbr_metallic_roughness.metallic_factor;
  float const roughness    = material.pbr_metallic_roughness.roughness_factor;

  Material*   new_material = World::MaterialManager().Construct(
      base_color_texture,
      normal_texture,
      metal_rough_texture,
      emissive_texture,
      sampler,
      Material::GpuRepr{
            .BaseColorTexture  = base_color_texture ? base_color_texture.GetSRVHandle() : SRVHandle{},
            .NormalTexture     = normal_texture ? normal_texture.GetSRVHandle() : SRVHandle{},
            .MetalRoughTexture = metal_rough_texture ? metal_rough_texture.GetSRVHandle() : SRVHandle{},
            .EmissiveTexture   = emissive_texture ? emissive_texture.GetSRVHandle() : SRVHandle{},
            .Sampler           = sampler ? sampler.GetSamplerHandle() : SamplerHandle{},
            .BaseColorFactor   = base_color_factor,
            .EmissiveFactor    = emissive_factor,
            .EmissiveStrength  = emissive_strength,
            .Metal             = metallic,
            .Rough             = roughness,
      } );

  model->AddMaterial( new_material );

  return new_material;
}

Ember::ModelLoader::ModelLoader( RenderDevice* render_device, World* world, TextureLoader* texture_loader )
  : m_RenderDevice{ render_device }, m_World{ world }, m_TextureLoader{ texture_loader }
{}

Ember::Model* Ember::ModelLoader::TryLoadModel( char const* filename )
{
  cgltf_data*   gltf_model = nullptr;
  cgltf_options options    = {};
  cgltf_result  result     = cgltf_parse_file( &options, filename, &gltf_model );

  if ( result != cgltf_result_success )
  {
    char buf[512];
    sprintf_s( buf, "%s failed to load", filename );
    OutputDebugStringA( buf );
    cgltf_free( gltf_model );

    return nullptr;
  }

  result = cgltf_validate( gltf_model );

  if ( result != cgltf_result_success )
  {
    char buf[512];
    OutputDebugStringA( buf );
    cgltf_free( gltf_model );

    return nullptr;
  }

  result = cgltf_load_buffers( &options, gltf_model, filename );

  if ( result != cgltf_result_success )
  {
    char buf[512];
    sprintf_s( buf, "%s buffers failed to load.", filename );
    OutputDebugStringA( buf );
    cgltf_free( gltf_model );

    return nullptr;
  }

  // Output data
  std::vector<Vertex>    vertices;
  std::vector<uint16_t>  indices;
  std::vector<Mesh*>     meshes;
  std::vector<Material*> materials;
  std::vector<Primitive> primitives;
  std::vector<Node*>     nodes;
  MeshData*              mesh_data     = World::MeshManager().Construct();

  Model*                 model         = m_World->CreateObject<Model>( mesh_data );

  LoadingContext         context       = { model, mesh_data, &vertices, &indices };

  cgltf_scene const*     current_scene = gltf_model->scene;
  for ( uint32_t node_idx = 0; node_idx < current_scene->nodes_count; ++node_idx )
  {
    ProcessNode( &context, model, *current_scene->nodes[node_idx] );
  }

  auto const vertex_buffer = m_RenderDevice->CreateVertexBuffer( ByteSizeOf( vertices ), sizeof( vertices[0] ) );
  vertex_buffer.Write( 0, ByteSizeOf( vertices ), DataOf( vertices ) );

  auto const index_buffer = m_RenderDevice->CreateIndexBuffer( ByteSizeOf( indices ), DXGI_FORMAT_R16_UINT );
  index_buffer.Write( 0, ByteSizeOf( indices ), DataOf( indices ) );

  mesh_data->VertexBuffer = vertex_buffer;
  mesh_data->IndexBuffer  = index_buffer;

  cgltf_free( gltf_model );

  Context::Receipt receipt = m_TextureLoader->EndBatch();
  m_RenderDevice->WaitOn( receipt );

  return model;
}

void Ember::ModelLoader::Update()
{}

void Ember::ModelLoader::FlushBarriers( ID3D12GraphicsCommandList* command_list )
{}
