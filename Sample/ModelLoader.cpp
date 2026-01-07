#include "ModelLoader.hpp"

#include <Graphics/RenderDevice.hpp>
#include <Util/DataUtil.hpp>
#include <Util/HelperUtils.hpp>
#include "BasicApp.hpp"
#include "Material.hpp"
#include "MaterialManager.hpp"

#include <meshoptimizer.h>
#include <mikktspace.h>

namespace
{

struct LoadingData
{
  DirectX::XMFLOAT3 Position;
  DirectX::XMFLOAT3 Normal;
  DirectX::XMFLOAT4 Tangent;
  DirectX::XMFLOAT4 Color{ 1.0f, 1.0f, 1.0f, 1.0f };
  DirectX::XMFLOAT2 TexCoord0;
  DirectX::XMFLOAT2 TexCoord1;
};

using Payload = std::span<LoadingData>;

void SetTangent(
    SMikkTSpaceContext const* ctx, float const out_tan[], float const sign, int const face_idx, int const vert_idx )
{
  Payload&     payload    = *( Payload* )ctx->m_pUserData;
  size_t const vertex_idx = 3 * face_idx + vert_idx;
  memcpy( &payload[vertex_idx].Tangent, out_tan, sizeof( float ) * 3 );
  payload[vertex_idx].Tangent.w = -sign;
}

int GetFaceCount( SMikkTSpaceContext const* ctx )
{
  Payload const& payload = *( Payload* )ctx->m_pUserData;
  return ( int )payload.size() / 3;
}

int GetNumFaceVertices( SMikkTSpaceContext const*, int const )
{
  return 3;
}

void GetPosition( SMikkTSpaceContext const* ctx, float out_pos[], int const face_idx, int const vert_idx )
{
  Payload const& payload    = *( Payload* )ctx->m_pUserData;
  size_t const   vertex_idx = 3 * face_idx + vert_idx;
  memcpy( out_pos, &payload[vertex_idx].Position, sizeof( float ) * 3 );
}

void GetNormal( SMikkTSpaceContext const* ctx, float out_norm[], int const face_idx, int const vert_idx )
{
  Payload const& payload    = *( Payload* )ctx->m_pUserData;
  size_t const   vertex_idx = 3 * face_idx + vert_idx;
  memcpy( out_norm, &payload[vertex_idx].Normal, sizeof( float ) * 3 );
}

void GetTexCoord( SMikkTSpaceContext const* ctx, float out_tex[], int const face_idx, int const vert_idx )
{
  Payload const& payload    = *( Payload* )ctx->m_pUserData;
  size_t const   vertex_idx = 3 * face_idx + vert_idx;
  memcpy( out_tex, &payload[vertex_idx].TexCoord0, sizeof( float ) * 2 );
}

std::pair<Ember::VertexLite, Ember::VertexData> QuantizeData( LoadingData const& in_data )
{
  DirectX::XMFLOAT3 norm;
  XMStoreFloat3( &norm, DirectX::XMVector3Normalize( XMLoadFloat3( &in_data.Normal ) ) );
  uint32_t const normal = meshopt_quantizeUnorm( norm.x * 0.5f + 0.5f, 10 ) |
                          ( meshopt_quantizeUnorm( norm.y * 0.5f + 0.5f, 10 ) << 10 ) |
                          ( meshopt_quantizeUnorm( norm.z * 0.5f + 0.5f, 10 ) << 20 );

  DirectX::XMFLOAT3 tang;
  XMStoreFloat3( &tang, DirectX::XMVector3Normalize( XMLoadFloat4( &in_data.Tangent ) ) );
  uint32_t const tangent = meshopt_quantizeUnorm( tang.x * 0.5f + 0.5f, 10 ) |
                           ( meshopt_quantizeUnorm( tang.y * 0.5f + 0.5f, 10 ) << 10 ) |
                           ( meshopt_quantizeUnorm( tang.z * 0.5f + 0.5f, 10 ) << 20 ) |
                           ( meshopt_quantizeUnorm( in_data.Tangent.w * 0.5f + 0.5f, 2 ) ) << 30;

  return {
    {
     .PositionX  = in_data.Position.x,
     .PositionY  = in_data.Position.y,
     .PositionZ  = in_data.Position.z,
     .PositionW  = 1,
     .TexCoord0X = in_data.TexCoord0.x,
     .TexCoord0Y = in_data.TexCoord0.y,
     .TexCoord1X = in_data.TexCoord1.x,
     .TexCoord1Y = in_data.TexCoord1.y,
     },
    {
     .QuantizedNormal  = normal,
     .QuantizedTangent = tangent,
     .Color            = in_data.Color,
     }
  };
}

} // namespace

void LoadAttribute(
    std::vector<LoadingData>* vertices,
    std::vector<byte>*        scratch,
    cgltf_accessor const*     accessor,
    size_t const              stride,
    size_t const              offset,
    size_t const              components )
{
  size_t const float_count = cgltf_accessor_unpack_floats( accessor, nullptr, 0 );
  ASSERT( float_count % components == 0 );
  scratch->resize( float_count * sizeof( float ) );
  cgltf_accessor_unpack_floats( accessor, ( float* )scratch->data(), float_count );

  size_t const element_count = float_count / components;
  // Guaranteed to have space for these vertices.
  vertices->resize( element_count );

  byte*        write_ptr = reinterpret_cast<byte*>( vertices->data() ) + offset;
  float const* read_ptr  = ( float* )scratch->data();
  for ( size_t i = 0; i < element_count; ++i )
  {
    memcpy( write_ptr, read_ptr, components * sizeof( float ) );

    read_ptr  += components;
    write_ptr += stride;
  }

  scratch->clear();
}

void Ember::ModelLoader::ProcessMesh( LoadingContext* context, flecs::entity owning, cgltf_mesh const& mesh ) const
{
  cgltf_primitive const* primitives = mesh.primitives;

  for ( uint32_t primitive_index = 0; primitive_index < mesh.primitives_count; ++primitive_index )
  {
    cgltf_primitive const& primitive = primitives[primitive_index];

    ProcessPrimitive( context, owning, primitive );
  }
}

bool Ember::ModelLoader::TryLoadTexture(
    Texture* texture, cgltf_image const& image, ColorSpaceOverride const color_space_override ) const
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

void Ember::ModelLoader::ProcessNode( LoadingContext* context, flecs::entity parent, cgltf_node const& node ) const
{
  DirectX::XMVECTOR translation{ DirectX::XMVectorSet( 0.0f, 0.0f, 0.0f, 1.0f ) };
  DirectX::XMVECTOR rotation{ DirectX::XMQuaternionIdentity() };
  DirectX::XMVECTOR scale{ DirectX::XMVectorSplatOne() };

  if ( node.has_matrix )
  {
    XMMatrixDecompose( &scale, &rotation, &translation, DirectX::XMMATRIX{ node.matrix } );
  }
  else
  {
    if ( node.has_translation )
      translation = DirectX::XMVectorSet( node.translation[0], node.translation[1], node.translation[2], 1.0f );
    if ( node.has_rotation )
      rotation = DirectX::XMVectorSet( node.rotation[0], node.rotation[1], node.rotation[2], node.rotation[3] );
    if ( node.has_scale ) scale = DirectX::XMVectorSet( node.scale[0], node.scale[1], node.scale[2], 1.0f );
  }

  bool const          has_translation = node.has_matrix or node.has_translation;
  bool const          has_rotation    = node.has_matrix or node.has_rotation;
  bool const          has_scale       = node.has_matrix or node.has_scale;

  flecs::entity const my_node         = m_World->GetECS().entity().child_of( parent ).add<WorldBoundingBox>();
  context->NodeCache[&node]           = my_node;

  if ( has_translation ) my_node.set<Translation>( Translation{ translation } );
  if ( has_rotation ) my_node.set<Rotation>( Rotation{ rotation } );
  if ( has_scale ) my_node.set<Scale>( Scale{ scale } );

  if ( node.mesh )
  {
    ProcessMesh( context, my_node, *node.mesh );
  }

  for ( uint32_t child_idx = 0; child_idx < node.children_count; ++child_idx )
  {
    ProcessNode( context, my_node, *node.children[child_idx] );
  }
}

cgltf_accessor* FindAccessor(
    cgltf_primitive const& primitive, cgltf_attribute_type const attribute_type, int const index = 0 )
{
  for ( int i = 0; i < primitive.attributes_count; i++ )
  {
    if ( primitive.attributes[i].type == attribute_type and primitive.attributes[i].index == index )
      return primitive.attributes[i].data;
  }
  return nullptr;
}

void Ember::ModelLoader::ProcessPrimitive(
    LoadingContext* context, flecs::entity owning, cgltf_primitive const& primitive ) const
{
  using namespace std::string_view_literals;

  // VertexStart is per-primitive
  int32_t const  vertex_start      = ( int32_t )context->VertexPositions.size();
  uint32_t const meshlet_start     = ( uint32_t )context->Meshlets.size();
  uint32_t const vertex_lite_start = ( uint32_t )context->VertexData.size();

  ASSERT( primitive.type == cgltf_primitive_type_triangles );

  size_t const index_start = context->Indices.size();

  // Index Buffer
  std::vector<uint32_t> loaded_indices;
  ASSERT(
      primitive.indices->type == cgltf_type_scalar and
      ( primitive.indices->component_type == cgltf_component_type_r_32u or
        primitive.indices->component_type == cgltf_component_type_r_16u or
        primitive.indices->component_type == cgltf_component_type_r_8u ) );
  size_t const index_count = cgltf_accessor_unpack_indices( primitive.indices, nullptr, sizeof( uint32_t ), 0 );
  ASSERT( index_count > 0 );
  loaded_indices.resize( index_count );
  cgltf_accessor_unpack_indices( primitive.indices, loaded_indices.data(), sizeof( uint32_t ), index_count );

  // MaterialImpl

  MaterialImpl* material = nullptr;
  if ( primitive.material )
  {
    // TODO: Default MaterialImpl.
    material = TryProcessMaterial( context, primitive.material );
  }
  else
  {
    material = GetDefaultMaterial( context );
  }

  DirectX::BoundingBox     prim_aabb;

  std::vector<byte>        scratch;
  std::vector<LoadingData> loaded_data;

  bool                     has_tangent = false;

  // TODO: Check and use cgltf_find_accessor.
  if ( auto* accessor = FindAccessor( primitive, cgltf_attribute_type_position ) )
  {
    ASSERT( accessor->component_type == cgltf_component_type_r_32f );
    ASSERT( accessor->type == cgltf_type_vec3 );

    auto              pos_min_v3 = DirectX::XMFLOAT3( accessor->min );
    DirectX::XMVECTOR pos_min    = XMLoadFloat3( &pos_min_v3 );
    auto              pos_max_v3 = DirectX::XMFLOAT3( accessor->max );
    auto              pos_max    = XMLoadFloat3( &pos_max_v3 );
    DirectX::BoundingBox::CreateFromPoints( prim_aabb, pos_min, pos_max );

    size_t constexpr stride     = sizeof( LoadingData );
    size_t constexpr offset     = offsetof( LoadingData, Position );
    size_t constexpr components = 3;

    LoadAttribute( &loaded_data, &scratch, accessor, stride, offset, components );
  }
  if ( auto* accessor = FindAccessor( primitive, cgltf_attribute_type_normal ) )
  {
    ASSERT( accessor->component_type == cgltf_component_type_r_32f );
    ASSERT( accessor->type == cgltf_type_vec3 );

    size_t constexpr stride     = sizeof( LoadingData );
    size_t constexpr offset     = offsetof( LoadingData, Normal );
    size_t constexpr components = 3;

    LoadAttribute( &loaded_data, &scratch, accessor, stride, offset, components );
  }
  if ( auto* accessor = FindAccessor( primitive, cgltf_attribute_type_tangent ) )
  {
    ASSERT( accessor->component_type == cgltf_component_type_r_32f );
    ASSERT( accessor->type == cgltf_type_vec4 );

    size_t constexpr stride     = sizeof( LoadingData );
    size_t constexpr offset     = offsetof( LoadingData, Tangent );
    size_t constexpr components = 4;

    LoadAttribute( &loaded_data, &scratch, accessor, stride, offset, components );

    has_tangent = true;
  }
  if ( auto* accessor = FindAccessor( primitive, cgltf_attribute_type_texcoord, 0 ) )
  {
    ASSERT( accessor->component_type == cgltf_component_type_r_32f );
    ASSERT( accessor->type == cgltf_type_vec2 );

    size_t constexpr stride     = sizeof( LoadingData );
    size_t constexpr offset     = offsetof( LoadingData, TexCoord0 );
    size_t constexpr components = 2;

    LoadAttribute( &loaded_data, &scratch, accessor, stride, offset, components );
  }
  if ( auto* accessor = FindAccessor( primitive, cgltf_attribute_type_texcoord, 1 ) )
  {
    ASSERT( accessor->component_type == cgltf_component_type_r_32f );
    ASSERT( accessor->type == cgltf_type_vec2 );

    size_t constexpr stride     = sizeof( LoadingData );
    size_t constexpr offset     = offsetof( LoadingData, TexCoord1 );
    size_t constexpr components = 2;

    LoadAttribute( &loaded_data, &scratch, accessor, stride, offset, components );
  }
  if ( auto* accessor = FindAccessor( primitive, cgltf_attribute_type_color, 0 ) )
  {
    ASSERT( accessor->component_type == cgltf_component_type_r_32f );

    size_t constexpr stride = sizeof( LoadingData );
    size_t constexpr offset = offsetof( LoadingData, Color );
    size_t components       = 3;
    switch ( accessor->type )
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

    LoadAttribute( &loaded_data, &scratch, accessor, stride, offset, components );
  }
  // TODO: Grab other attributes.

  // Tangent Loading
  if ( not has_tangent )
  {
    // Flatten vertices.
    scratch.resize( sizeof( LoadingData ) * index_count );

    {
      LoadingData* write_ptr = ( LoadingData* )scratch.data();
      LoadingData* read_ptr  = loaded_data.data();
      for ( uint32_t const index : loaded_indices )
      {
        memcpy( write_ptr, read_ptr + index, sizeof( LoadingData ) );
        write_ptr++;
      }
    }

    {
      Payload              payload{ ( LoadingData* )scratch.data(), index_count };

      SMikkTSpaceInterface mikk_t_space_interface;
      ZeroMemory( &mikk_t_space_interface, sizeof mikk_t_space_interface );
      mikk_t_space_interface.m_getNumFaces          = &GetFaceCount;
      mikk_t_space_interface.m_getNumVerticesOfFace = &GetNumFaceVertices;
      mikk_t_space_interface.m_getPosition          = &GetPosition;
      mikk_t_space_interface.m_getNormal            = &GetNormal;
      mikk_t_space_interface.m_getTexCoord          = &GetTexCoord;
      mikk_t_space_interface.m_setTSpaceBasic       = &SetTangent;

      SMikkTSpaceContext mikk_t_space_context{
        .m_pInterface = &mikk_t_space_interface,
        .m_pUserData  = &payload,
      };

      ENSURE( genTangSpaceDefault( &mikk_t_space_context ) );
    }

    // Weld back to vertex and index buffers.
    size_t const             unindexed_vertex_count = index_count;
    LoadingData const* const unindexed_vertices     = ( LoadingData* )scratch.data();
    std::vector<uint32_t>    remap( unindexed_vertex_count );
    size_t const             vertex_count = meshopt_generateVertexRemap(
        remap.data(), nullptr, index_count, unindexed_vertices, unindexed_vertex_count, sizeof( LoadingData ) );

    loaded_data.resize( vertex_count );
    loaded_indices.resize( index_count );

    meshopt_remapIndexBuffer( loaded_indices.data(), nullptr, index_count, remap.data() );
    meshopt_remapVertexBuffer(
        loaded_data.data(), unindexed_vertices, unindexed_vertex_count, sizeof( LoadingData ), remap.data() );
  }

  size_t const vertex_count = loaded_data.size();

  // Cluster meshlets
  std::vector<meshopt_Meshlet> meshlets;
  std::vector<uint32_t>        meshlet_vertices;
  std::vector<byte>            meshlet_triangles;
  {
    size_t max_meshlets = meshopt_buildMeshletsBound( loaded_indices.size(), kMaxVertices, kMaxTriangles );
    meshlets.resize( max_meshlets );
    meshlet_vertices.resize( max_meshlets * kMaxVertices );
    meshlet_triangles.resize( max_meshlets * kMaxTriangles * 3 );

    size_t meshlet_count = meshopt_buildMeshlets(
        meshlets.data(),
        meshlet_vertices.data(),
        meshlet_triangles.data(),
        loaded_indices.data(),
        loaded_indices.size(),
        ( float* )&loaded_data[0].Position,
        loaded_data.size(),
        sizeof( LoadingData ),
        kMaxVertices,
        kMaxTriangles,
        kConeWeight );
    meshopt_Meshlet const& last = meshlets[meshlet_count - 1];

    meshlet_vertices.resize( last.vertex_offset + last.vertex_count );
    meshlet_triangles.resize( last.triangle_offset + ( ( last.triangle_count * 3 + 3 ) & ~3 ) );
    meshlets.resize( meshlet_count );
  }

  // TODO: Something wrong with my understanding of how the meshlet data is used.

  // Quantization
  {
    scratch.resize( ( sizeof( VertexData ) + sizeof( VertexLite ) ) * vertex_count );

    LoadingData const* read_ptr   = loaded_data.data();
    VertexLite*        write_lite = ( VertexLite* )scratch.data();
    VertexData*        write_data = ( VertexData* )( scratch.data() + sizeof( VertexLite ) * vertex_count );
    for ( int i = 0; i < vertex_count; i++ )
    {
      std::tie( *write_lite, *write_data ) = QuantizeData( *read_ptr );
      write_lite++;
      write_data++;
      read_ptr++;
    }
  }

  // Finalize
  {
    VertexLite* begin = ( VertexLite* )scratch.data();
    VertexLite* end   = begin + vertex_count;
    context->VertexPositions.insert( context->VertexPositions.end(), begin, end );
  }
  {
    VertexData* begin = ( VertexData* )( scratch.data() + sizeof( VertexLite ) * vertex_count );
    VertexData* end   = begin + vertex_count;
    context->VertexData.insert( context->VertexData.end(), begin, end );
  }
  context->Indices.insert( context->Indices.end(), loaded_indices.begin(), loaded_indices.end() );

  uint32_t meshlet_vert_start = ( uint32_t )context->MeshletVertices.size();
  uint32_t triangle_start     = ( uint32_t )context->MeshletTriangles.size();
  std::ranges::transform(
      meshlets,
      std::back_inserter( context->Meshlets ),
      [&]( meshopt_Meshlet const& m )
      {
        meshopt_Bounds bounds = meshopt_computeMeshletBounds(
            meshlet_vertices.data() + m.vertex_offset,
            meshlet_triangles.data() + m.triangle_offset,
            m.triangle_count,
            ( float* )&loaded_data[0].Position,
            vertex_count,
            StrideOf( loaded_data ) );

        uint32_t const cone_info = ( meshopt_quantizeUnorm( 0.5f * bounds.cone_axis[0] + 0.5f, 8 ) ) |
                                   ( meshopt_quantizeUnorm( 0.5f * bounds.cone_axis[1] + 0.5f, 8 ) << 8 ) |
                                   ( meshopt_quantizeUnorm( 0.5f * bounds.cone_axis[2] + 0.5f, 8 ) << 16 ) |
                                   ( meshopt_quantizeUnorm( 0.5f * bounds.cone_cutoff + 0.5f, 8 ) << 24 );

        uint8_t apex[3];
        for ( int i = 0; i < 3; i++ )
        {
          float ap  = bounds.cone_apex[i];
          ap       -= bounds.center[i];
          ap       /= bounds.radius;
          ap        = 0.5f + 0.5f * ap;
          apex[i]   = ( uint8_t )meshopt_quantizeUnorm( ap, 8 );
        }
        uint32_t const cone_apex = apex[0] | apex[1] << 8 | apex[2] << 16;

        return Meshlet{
          .VertexOffset   = m.vertex_offset + meshlet_vert_start,
          .TriangleOffset = m.triangle_offset + triangle_start,
          .VertexCount    = m.vertex_count,
          .TriangleCount  = m.triangle_count,
          .CenterX        = bounds.center[0],
          .CenterY        = bounds.center[1],
          .CenterZ        = bounds.center[2],
          .Radius         = bounds.radius,
          .ConeInfo       = cone_info,
          .ConeApexOffset = cone_apex,
        };
      } );

  context->MeshletTriangles.insert(
      context->MeshletTriangles.end(), meshlet_triangles.begin(), meshlet_triangles.end() );
  context->MeshletVertices.insert( context->MeshletVertices.end(), meshlet_vertices.begin(), meshlet_vertices.end() );

  GeometryImpl* geometry = World::GeometryManager().Copy( context->Geometry );

  ( void )m_World->GetECS()
      .entity()
      .insert(
          [&]( WorldBoundingBox&, WorldTransform&, Mesh& prim, Material& mat, Geometry& geom, LocalBoundingBox& bb )
          {
            mat  = Material{ material };
            geom = Geometry{ geometry };
            prim = {
              .FirstIndex      = ( uint32_t )index_start,
              .IndexCount      = ( uint32_t )index_count,
              .VertexDataStart = ( uint32_t )vertex_start,
              .VertexLiteStart = vertex_lite_start,
              .VertexCount     = ( uint32_t )vertex_count,
              .MeshletCount    = ( uint32_t )meshlets.size(),
              .FirstMeshlet    = meshlet_start,
            };
            bb.AABB = prim_aabb;
          } )
      .child_of( owning );
}

Ember::MaterialImpl* Ember::ModelLoader::TryProcessMaterial(
    LoadingContext* context, cgltf_material const* material ) const
{
  if ( auto location = context->MaterialCache.find( material ); location != context->MaterialCache.end() )
  {
    return World::MaterialManager().Copy( location->second );
  }

  ASSERT( material->has_pbr_metallic_roughness );

  auto const base_color_factor = DirectX::XMFLOAT4{ material->pbr_metallic_roughness.base_color_factor };
  float      max_em            = std::max(
      1.0f,
      std::max(
          material->emissive_factor[0], std::max( material->emissive_factor[1], material->emissive_factor[2] ) ) );

  auto const emissive_factor = DirectX::XMFLOAT4{
    material->emissive_factor[0] / max_em,
    material->emissive_factor[1] / max_em,
    material->emissive_factor[2] / max_em,
    0.0f,
  };

  Float16 const emissive_strength = std::max( material->emissive_strength.emissive_strength, 1.0f ) * max_em;

  Texture       base_color_texture;
  Texture       normal_texture;
  Texture       metal_rough_texture;
  Texture       emissive_texture;

  uint32_t      base_color_texture_idx  = 0;
  uint32_t      normal_texture_idx      = 0;
  uint32_t      metal_rough_texture_idx = 0;
  uint32_t      emissive_texture_idx    = 0;

  if ( auto const& tex_view = material->pbr_metallic_roughness.base_color_texture; tex_view.texture )
  {
    cgltf_image const* base_color_image = tex_view.texture->image;
    base_color_texture_idx              = tex_view.texcoord;

    if ( not TryLoadTexture( &base_color_texture, *base_color_image, ColorSpaceOverride::kSrgb ) )
    {
      return nullptr;
    }
  }

  if ( auto const& tex_view = material->pbr_metallic_roughness.metallic_roughness_texture; tex_view.texture )
  {
    cgltf_image const* metal_rough_image = tex_view.texture->image;
    metal_rough_texture_idx              = tex_view.texcoord;

    if ( not TryLoadTexture( &metal_rough_texture, *metal_rough_image, ColorSpaceOverride::kLinear ) )
    {
      return nullptr;
    }
  }

  if ( auto const& tex_view = material->normal_texture; tex_view.texture )
  {
    cgltf_image const* normal_image = tex_view.texture->image;
    normal_texture_idx              = tex_view.texcoord;

    if ( not TryLoadTexture( &normal_texture, *normal_image, ColorSpaceOverride::kLinear ) )
    {
      return nullptr;
    }
  }

  if ( auto const& tex_view = material->emissive_texture; tex_view.texture )
  {
    cgltf_image const* emissive_image = tex_view.texture->image;
    emissive_texture_idx              = tex_view.texcoord;

    if ( not TryLoadTexture( &emissive_texture, *emissive_image, ColorSpaceOverride::kSrgb ) )
    {
      return nullptr;
    }
  }

  Float16 const metallic     = material->pbr_metallic_roughness.metallic_factor;
  Float16 const roughness    = material->pbr_metallic_roughness.roughness_factor;
  Float16 const alpha_cutoff = material->alpha_cutoff;

  AlphaMode     alpha_mode;
  switch ( material->alpha_mode )
  {
    case cgltf_alpha_mode_opaque:
      alpha_mode = AlphaMode::kOpaque;
      break;
    case cgltf_alpha_mode_mask:
      alpha_mode = AlphaMode::kMask;
      break;
    case cgltf_alpha_mode_blend:
      alpha_mode = AlphaMode::kBlend;
      break;
    default:
      UNREACHABLE;
  }

  Color32 packed_emissive_factor = emissive_factor;

  ASSERT( base_color_texture_idx < 4 );
  ASSERT( normal_texture_idx < 4 );
  ASSERT( metal_rough_texture_idx < 4 );
  ASSERT( emissive_texture_idx < 4 );
  // Pack texture coordinate indices into emissive_factor.A
  packed_emissive_factor.A       = ( byte )( base_color_texture_idx | ( normal_texture_idx << 2 ) |
                                       ( metal_rough_texture_idx << 4 ) | ( emissive_texture_idx << 6 ) );

  MaterialHandle material_handle = m_MaterialManager->CreateMaterialHandle( {
      .BaseColorTexture  = base_color_texture ? base_color_texture.GetSRVHandle() : SRVHandle{},
      .NormalTexture     = normal_texture ? normal_texture.GetSRVHandle() : SRVHandle{},
      .MetalRoughTexture = metal_rough_texture ? metal_rough_texture.GetSRVHandle() : SRVHandle{},
      .EmissiveTexture   = emissive_texture ? emissive_texture.GetSRVHandle() : SRVHandle{},
      .BaseColorFactor   = base_color_factor,
      .EmissiveFactor    = packed_emissive_factor,
      .EmissiveStrength  = emissive_strength,
      .Metal             = metallic,
      .Rough             = roughness,
      .AlphaCutoff       = alpha_cutoff,
  } );

  MaterialImpl*  new_material    = World::MaterialManager().Construct(
      base_color_texture,
      normal_texture,
      metal_rough_texture,
      emissive_texture,
      m_MaterialManager,
      material_handle,
      alpha_mode );

  context->MaterialCache.insert_or_assign( material, new_material );

  return new_material;
}

Ember::MaterialImpl* Ember::ModelLoader::GetDefaultMaterial( LoadingContext* context ) const
{
  if ( auto location = context->MaterialCache.find( nullptr ); location != context->MaterialCache.end() )
  {
    return World::MaterialManager().Copy( location->second );
  }

  MaterialHandle material_handle = m_MaterialManager->CreateMaterialHandle( {
      .BaseColorTexture  = SRVHandle{},
      .NormalTexture     = SRVHandle{},
      .MetalRoughTexture = SRVHandle{},
      .EmissiveTexture   = SRVHandle{},
      .BaseColorFactor   = DirectX::XMFLOAT4{ 1.0f, 1.0f, 1.0f, 1.0f },
      .EmissiveFactor    = DirectX::XMFLOAT4{ 0.0f, 0.0f, 0.0f, 0.0f },
      .EmissiveStrength  = 1.0f,
      .Metal             = 0.0f,
      .Rough             = 1.0f,
  } );

  auto           new_material    = World::MaterialManager().Construct(
      Texture{}, Texture{}, Texture{}, Texture{}, m_MaterialManager, material_handle, AlphaMode::kOpaque );
  context->MaterialCache.insert_or_assign( nullptr, new_material );

  return new_material;
}

Ember::ModelLoader::ModelLoader(
    RenderDevice*            render_device,
    World*                   world,
    std::shared_ptr<Context> compute_context,
    TextureLoader*           texture_loader,
    MaterialManager*         material_manager,
    GeometryManager*         geometry_manager )
  : m_RenderDevice{ render_device }
  , m_World{ world }
  , m_ComputeContext{ std::move( compute_context ) }
  , m_TextureLoader{ texture_loader }
  , m_MaterialManager{ material_manager }
  , m_GeometryManager{ geometry_manager }
{}

void Ember::ModelLoader::ProcessAnimation( LoadingContext* context, cgltf_animation const& animation ) const
{
  float              track_length = 0.0f;
  std::vector<float> timeline;
  for ( uint32_t channel_idx = 0; channel_idx < animation.channels_count; ++channel_idx )
  {
    cgltf_animation_sampler const& sampler     = *animation.channels[channel_idx].sampler;

    size_t const                   float_count = cgltf_accessor_unpack_floats( sampler.input, nullptr, 0 );
    timeline.resize( float_count );
    cgltf_accessor_unpack_floats( sampler.input, timeline.data(), float_count );

    track_length = std::max( track_length, timeline.back() );
  }

  for ( uint32_t channel_idx = 0; channel_idx < animation.channels_count; ++channel_idx )
  {
    cgltf_animation_channel const& channel = animation.channels[channel_idx];

    timeline.clear();
    cgltf_animation_sampler const& sampler = *channel.sampler;
    {
      size_t const float_count = cgltf_accessor_unpack_floats( sampler.input, nullptr, 0 );
      timeline.resize( float_count );
      cgltf_accessor_unpack_floats( sampler.input, timeline.data(), float_count );
    }

    flecs::entity target_entity = context->NodeCache[channel.target_node];

    switch ( channel.target_path )
    {
      case cgltf_animation_path_type_translation:
      {
        std::vector<DirectX::XMFLOAT3> translations;
        size_t const                   float_count = cgltf_accessor_unpack_floats( sampler.output, nullptr, 0 );
        ASSERT( float_count % 3 == 0 );
        size_t const float3_count = float_count / 3;
        translations.resize( float3_count );
        cgltf_accessor_unpack_floats( sampler.output, ( float* )translations.data(), float_count );

        target_entity.ensure<TranslatingAnimation>().Animations[animation.name] = {
            .Keyframes={
              std::move( timeline ),
              std::move( translations ),
            },
            .Length = track_length,
          };
      }
      break;
      case cgltf_animation_path_type_rotation:
      {
        std::vector<DirectX::XMFLOAT4> rotations;
        size_t const                   float_count = cgltf_accessor_unpack_floats( sampler.output, nullptr, 0 );
        ASSERT( float_count % 4 == 0 );
        size_t const float4_count = float_count / 4;
        rotations.resize( float4_count );
        cgltf_accessor_unpack_floats( sampler.output, ( float* )rotations.data(), float_count );

        target_entity.ensure<RotatingAnimation>().Animations[animation.name] = {
            .Keyframes= {
              std::move( timeline ),
              std::move( rotations ),
            },
            .Length = track_length,
          };
      }
      break;
      case cgltf_animation_path_type_scale:
      {
        std::vector<DirectX::XMFLOAT3> scales;
        size_t const                   float_count = cgltf_accessor_unpack_floats( sampler.output, nullptr, 0 );
        ASSERT( float_count % 3 == 0 );
        size_t const float3_count = float_count / 3;
        scales.resize( float3_count );
        cgltf_accessor_unpack_floats( sampler.output, ( float* )scales.data(), float_count );

        target_entity.ensure<ScalingAnimation>().Animations[animation.name] = {
            .Keyframes = {
              std::move( timeline ),
              std::move( scales ),
            },
            .Length = track_length,
          };
      }
      break;
      case cgltf_animation_path_type_weights:
        UNIMPLEMENTED;
      default:
        UNREACHABLE;
    }
  }
}

void Ember::ModelLoader::FinalizeGeometry( LoadingContext* context, flecs::entity entity ) const
{
  GeometryAllocation geom;
  uint32_t           vertex_position_offset;
  uint32_t           vertex_data_offset;
  uint32_t           meshlet_offset;
  uint32_t           meshlet_triangle_offset;
  uint32_t           meshlet_vertices_offset;
  uint32_t           indices_offset;
  {
    uint32_t vertex_position_size       = ByteSizeOf( context->VertexPositions );
    uint32_t vertex_data_size           = ByteSizeOf( context->VertexData );
    uint32_t meshlet_size               = ByteSizeOf( context->Meshlets );
    uint32_t meshlet_triangle_size      = ByteSizeOf( context->MeshletTriangles );
    uint32_t meshlet_vertices_size      = ByteSizeOf( context->MeshletVertices );
    uint32_t indices_size               = ByteSizeOf( context->Indices );

    uint32_t vertex_position_alignment  = StrideOf( context->VertexPositions );
    uint32_t vertex_data_alignment      = StrideOf( context->VertexData );
    uint32_t meshlet_alignment          = StrideOf( context->Meshlets );
    uint32_t meshlet_triangle_alignment = 4;
    uint32_t meshlet_vertices_alignment = StrideOf( context->MeshletVertices );
    uint32_t indices_alignment          = 4;

    uint32_t largest_alignment          = 4;
    largest_alignment                   = std::max( largest_alignment, vertex_position_alignment );
    largest_alignment                   = std::max( largest_alignment, vertex_data_alignment );
    largest_alignment                   = std::max( largest_alignment, meshlet_alignment );
    largest_alignment                   = std::max( largest_alignment, meshlet_triangle_alignment );
    largest_alignment                   = std::max( largest_alignment, meshlet_vertices_alignment );
    largest_alignment                   = std::max( largest_alignment, indices_alignment );

    uint32_t const total_size = vertex_position_size + vertex_data_size + meshlet_size + meshlet_triangle_size +
                                meshlet_vertices_size + indices_size + largest_alignment * 6;

    geom                             = m_GeometryManager->CreateGeometry( total_size, largest_alignment );

    uint32_t const base_offset_begin = geom.GetOffsetInBytes();
    uint32_t const base_offset_end   = base_offset_begin + total_size;

    uint32_t       offset            = base_offset_begin;

    vertex_position_offset           = offset + ( vertex_position_alignment - ( offset % vertex_position_alignment ) );
    offset                           = vertex_position_offset + vertex_position_size;
    ASSERT( offset <= base_offset_end );

    vertex_data_offset = offset + ( vertex_data_alignment - ( offset % vertex_data_alignment ) );
    offset             = vertex_data_offset + vertex_data_size;
    ASSERT( offset <= base_offset_end );

    meshlet_offset = offset + ( meshlet_alignment - ( offset % meshlet_alignment ) );
    offset         = meshlet_offset + meshlet_size;
    ASSERT( offset <= base_offset_end );

    meshlet_triangle_offset = offset + ( meshlet_triangle_alignment - ( offset % meshlet_triangle_alignment ) );
    offset                  = meshlet_triangle_offset + meshlet_triangle_size;
    ASSERT( offset <= base_offset_end );

    meshlet_vertices_offset = offset + ( meshlet_vertices_alignment - ( offset % meshlet_vertices_alignment ) );
    offset                  = meshlet_vertices_offset + meshlet_vertices_size;
    ASSERT( offset <= base_offset_end );

    indices_offset = offset + ( indices_alignment - ( offset % indices_alignment ) );
    offset         = indices_offset + indices_size;
    ASSERT( offset <= base_offset_end );

    ASSERT( meshlet_offset % meshlet_alignment == 0 );
    ASSERT( meshlet_triangle_offset % meshlet_triangle_alignment == 0 );
    ASSERT( meshlet_vertices_offset % meshlet_vertices_alignment == 0 );

    for ( auto& meshlet : context->Meshlets )
    {
      meshlet.TriangleOffset += meshlet_triangle_offset;
      meshlet.VertexOffset   += meshlet_vertices_offset / 4;
    }

    geom.Write(
        vertex_position_offset - base_offset_begin,
        ByteSizeOf( context->VertexPositions ),
        DataOf( context->VertexPositions ) );
    geom.Write(
        vertex_data_offset - base_offset_begin, ByteSizeOf( context->VertexData ), DataOf( context->VertexData ) );
    geom.Write( meshlet_offset - base_offset_begin, ByteSizeOf( context->Meshlets ), DataOf( context->Meshlets ) );
    geom.Write(
        meshlet_triangle_offset - base_offset_begin,
        ByteSizeOf( context->MeshletTriangles ),
        DataOf( context->MeshletTriangles ) );
    geom.Write(
        meshlet_vertices_offset - base_offset_begin,
        ByteSizeOf( context->MeshletVertices ),
        DataOf( context->MeshletVertices ) );
    geom.Write( indices_offset - base_offset_begin, ByteSizeOf( context->Indices ), DataOf( context->Indices ) );
  }

  std::queue<flecs::entity> bfs_subtree;
  bfs_subtree.push( entity );
  while ( not bfs_subtree.empty() )
  {
    flecs::entity ent = bfs_subtree.front();
    bfs_subtree.pop();

    ent.get(
        [&]( Mesh& mesh )
        {
          mesh.FirstIndex      += indices_offset / sizeof( uint32_t );
          mesh.VertexDataStart += vertex_data_offset / sizeof( VertexData );
          mesh.VertexLiteStart += vertex_position_offset / sizeof( VertexLite );
          mesh.FirstMeshlet    += meshlet_offset / sizeof( Meshlet );
        } );

    ent.children( [&]( flecs::entity child ) { bfs_subtree.push( child ); } );
  }

  context->Geometry->GeometryAlloc = std::move( geom );

  // Store offsets for acceleration structure building.
  context->Offsets = {
    .VertexPositions  = vertex_position_offset,
    .VertexData       = vertex_data_offset,
    .Meshlets         = meshlet_offset,
    .MeshletTriangles = meshlet_triangle_offset,
    .MeshletVertices  = meshlet_vertices_offset,
    .Indices          = indices_offset,
  };
}

void Ember::ModelLoader::CreateAccelerationStructure( LoadingContext* context, flecs::entity root ) const
{
  auto const                 base_addr = context->Geometry->GeometryAlloc.GetBaseGPUVirtualAddress();

  std::vector<flecs::entity> meshes;
  std::queue<flecs::entity>  bfs_subtree;
  bfs_subtree.push( root );
  while ( not bfs_subtree.empty() )
  {
    flecs::entity ent = bfs_subtree.front();
    bfs_subtree.pop();

    if ( ent.has<Mesh>() ) meshes.push_back( ent );

    ent.children( [&]( flecs::entity child ) { bfs_subtree.push( child ); } );
  }

  ENSURE( not meshes.empty() );
  for ( flecs::entity entity : meshes )
  {
    auto mesh          = entity.get<Mesh>();

    auto index_offset  = mesh.FirstIndex * sizeof( uint32_t );
    auto vertex_offset = mesh.VertexLiteStart * sizeof( VertexLite );
    auto index_end     = index_offset + mesh.IndexCount * sizeof( uint32_t );
    auto vertex_end    = vertex_offset + mesh.VertexCount * sizeof( VertexLite );
    ASSERT( index_offset > vertex_end or index_end < vertex_offset );
    entity.set( CreateBLAS(
        context,
        base_addr + mesh.FirstIndex * sizeof( uint32_t ),
        base_addr + mesh.VertexLiteStart * sizeof( VertexLite ),
        mesh.IndexCount,
        mesh.VertexCount ) );
  }
}

Ember::BLAS Ember::ModelLoader::CreateBLAS(
    LoadingContext*                 context,
    D3D12_GPU_VIRTUAL_ADDRESS const index_addr,
    D3D12_GPU_VIRTUAL_ADDRESS const vert_addr,
    uint32_t const                  index_count,
    uint32_t const                  vertex_count ) const
{
  auto* cmd = &context->CommandList;

  D3D12_RAYTRACING_GEOMETRY_TRIANGLES_DESC const triangles_desc{
        .Transform3x4 = NULL,
        .IndexFormat  = DXGI_FORMAT_R32_UINT,
        .VertexFormat = DXGI_FORMAT_R16G16B16A16_FLOAT,
        .IndexCount   = index_count,
        .VertexCount  = vertex_count,
        .IndexBuffer  = index_addr,
        .VertexBuffer = {
          .StartAddress = vert_addr,
          .StrideInBytes = sizeof( VertexLite ),
        },
  };

  D3D12_RAYTRACING_GEOMETRY_DESC const blas_geometry{
    .Type      = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES,
    .Flags     = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE,
    .Triangles = triangles_desc,
  };

  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS const blas_inputs{
    .Type           = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL,
    .Flags          = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE,
    .NumDescs       = 1,
    .DescsLayout    = D3D12_ELEMENTS_LAYOUT_ARRAY,
    .pGeometryDescs = &blas_geometry,
  };

  ComPtr<ID3D12Device5> device;
  {
    ComPtr<ID3D12Device2> device2 = m_RenderDevice->GetDevice();
    ERR_ABORT( device2.As( &device ) );
  }

  D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO blas_prebuild_info;
  device->GetRaytracingAccelerationStructurePrebuildInfo( &blas_inputs, &blas_prebuild_info );

  ComPtr<ID3D12Resource>      staging_res;
  ComPtr<D3D12MA::Allocation> staging_alloc;
  {
    auto const resource_desc = CD3DX12_RESOURCE_DESC::Buffer(
        blas_prebuild_info.ScratchDataSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS );

#if not defined( RENDERDOC_COMPAT )
    D3D12MA::ALLOCATION_DESC const allocation_desc = {
      .Flags    = D3D12MA::ALLOCATION_FLAG_NONE,
      .HeapType = D3D12_HEAP_TYPE_DEFAULT,
    };

    ERR_ABORT( m_RenderDevice->GetAllocator()->CreateResource(
        &allocation_desc,
        &resource_desc,
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        staging_alloc.GetAddressOf(),
        IID_PPV_ARGS( &staging_res ) ) );

    cmd->Track( std::move( staging_alloc ) );
#else
    auto heap_property = CD3DX12_HEAP_PROPERTIES{ D3D12_HEAP_TYPE_DEFAULT };
    ERR_ABORT( m_RenderDevice->GetDevice()->CreateCommittedResource(
        &heap_property,
        D3D12_HEAP_FLAG_NONE,
        &resource_desc,
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS( &staging_res ) ) );
#endif
  }
  cmd->Track( staging_res );

  auto blas = m_RenderDevice->CreateASBuffer( blas_prebuild_info.ResultDataMaxSizeInBytes );

  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC const desc{
    .DestAccelerationStructureData    = blas.GetGPUVirtualAddress(),
    .Inputs                           = blas_inputs,
    .ScratchAccelerationStructureData = staging_res->GetGPUVirtualAddress(),
  };

  cmd->Get()->BuildRaytracingAccelerationStructure( &desc, 0, nullptr );
  cmd->Track( blas.GetBuffer() );

  // TODO: BLAS Compression
  return { std::move( blas ) };
}

std::expected<flecs::entity, Ember::ModelLoader::Error> Ember::ModelLoader::TryLoadModel( char const* filename )
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

    return std::unexpected{ Error::kCannotOpenFile };
  }

  result = cgltf_validate( gltf_model );

  if ( result != cgltf_result_success )
  {
    char buf[512];
    sprintf_s( buf, "%s is invalid", filename );
    OutputDebugStringA( buf );
    cgltf_free( gltf_model );

    return std::unexpected{ Error::kInvalidFile };
  }

  result = cgltf_load_buffers( &options, gltf_model, filename );

  if ( result != cgltf_result_success )
  {
    char buf[512];
    sprintf_s( buf, "%s buffers failed to load.", filename );
    OutputDebugStringA( buf );
    cgltf_free( gltf_model );

    return std::unexpected{ Error::kCannotLoadMemory };
  }

  auto           entity  = m_World->GetECS().entity().insert( [&]( WorldTransform&, WorldBoundingBox& ) {} );

  LoadingContext context = {
    .Geometry    = World::GeometryManager().Construct(),
    .CommandList = m_ComputeContext->GetCommandList(),
  };

  cgltf_scene const* current_scene = gltf_model->scene;
  for ( uint32_t node_idx = 0; node_idx < current_scene->nodes_count; ++node_idx )
  {
    ProcessNode( &context, entity, *current_scene->nodes[node_idx] );
  }

  // Textures should all be loaded by this point.
  auto const tex_receipt = m_TextureLoader->EndBatch();

  for ( uint32_t anim_idx = 0; anim_idx < gltf_model->animations_count; ++anim_idx )
  {
    ProcessAnimation( &context, gltf_model->animations[anim_idx] );
  }

  FinalizeGeometry( &context, entity );

  CreateAccelerationStructure( &context, entity );

  auto const self_receipt = m_ComputeContext->Submit( std::move( context.CommandList ) );

  cgltf_free( gltf_model );

  // One spare reference to GeometryImpl needs to be cleaned up.
  // GeometryImpl now owned solely by the Mesh components.
  World::GeometryManager().Destroy( context.Geometry );

  m_RenderDevice->WaitOn( self_receipt );
  m_RenderDevice->WaitOn( tex_receipt );

  return entity;
}
