#include "ModelLoader.hpp"

#include "BasicApp.hpp"
#include "Material.hpp"
#include "MaterialManager.hpp"
#include "RenderDevice.hpp"
#include "Util/DataUtil.hpp"
#include "Util/HelperUtils.hpp"

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

namespace Internal
{
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

} // namespace Internal

Ember::VertexData QuantizeData( LoadingData const& in_data )
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

  return Ember::VertexData{
    .PositionX        = in_data.Position.x,
    .PositionY        = in_data.Position.y,
    .PositionZ        = in_data.Position.z,
    .PositionW        = 1,
    .QuantizedNormal  = normal,
    .QuantizedTangent = tangent,
    .Color            = in_data.Color,
    .TexCoord0X       = in_data.TexCoord0.x,
    .TexCoord0Y       = in_data.TexCoord0.y,
    .TexCoord1X       = in_data.TexCoord1.x,
    .TexCoord1Y       = in_data.TexCoord1.y,
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

flecs::entity Ember::ModelLoader::ProcessNode( LoadingContext* context, flecs::entity parent, cgltf_node const& node )
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

  auto const my_node = m_World->GetECS().entity().child_of( parent ).insert(
      [&]( LocalTransform& lt, WorldTransform&, WorldBoundingBox& )
      {
        lt.Translation = translation;
        lt.Rotation    = rotation;
        lt.Scale       = scale;
      } );

  if ( node.mesh )
  {
    ProcessMesh( context, my_node, *node.mesh );
  }

  for ( uint32_t child_idx = 0; child_idx < node.children_count; ++child_idx )
  {
    ProcessNode( context, my_node, *node.children[child_idx] );
  }

  return my_node;
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
  int32_t const  vertex_start  = ( int32_t )context->VertexPositions.size();
  uint32_t const meshlet_start = ( uint32_t )context->Meshlets.size();

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
      mikk_t_space_interface.m_getNumFaces          = &Internal::GetFaceCount;
      mikk_t_space_interface.m_getNumVerticesOfFace = &Internal::GetNumFaceVertices;
      mikk_t_space_interface.m_getPosition          = &Internal::GetPosition;
      mikk_t_space_interface.m_getNormal            = &Internal::GetNormal;
      mikk_t_space_interface.m_getTexCoord          = &Internal::GetTexCoord;
      mikk_t_space_interface.m_setTSpaceBasic       = &Internal::SetTangent;

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
    const meshopt_Meshlet& last = meshlets[meshlet_count - 1];

    meshlet_vertices.resize( last.vertex_offset + last.vertex_count );
    meshlet_triangles.resize( last.triangle_offset + ( ( last.triangle_count * 3 + 3 ) & ~3 ) );
    meshlets.resize( meshlet_count );
  }

  // TODO: Something wrong with my understanding of how the meshlet data is used.

  // Quantization
  {
    scratch.resize( sizeof( VertexData ) * vertex_count );

    LoadingData const* read_ptr  = loaded_data.data();
    VertexData*        write_ptr = ( VertexData* )scratch.data();
    for ( int i = 0; i < vertex_count; i++ )
    {
      *write_ptr = QuantizeData( *read_ptr );
      write_ptr++;
      read_ptr++;
    }
  }

  // Finalize
  VertexData* begin = ( VertexData* )scratch.data();
  VertexData* end   = begin + vertex_count;
  context->VertexData.insert( context->VertexData.end(), begin, end );
  context->Indices.insert( context->Indices.end(), loaded_indices.begin(), loaded_indices.end() );

  context->VertexPositions.reserve( context->VertexData.size() );

  std::transform(
      begin,
      end,
      std::back_inserter( context->VertexPositions ),
      []( VertexData const& vd ) { return ShadowVertex{ vd.PositionX, vd.PositionY, vd.PositionZ, vd.PositionW }; } );

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
          [&]( WorldTransform&,
               LocalTransform&,
               WorldBoundingBox&,
               Mesh&             prim,
               Material&         mat,
               Geometry&         geom,
               LocalBoundingBox& bb )
          {
            mat  = Material{ material };
            geom = Geometry{ geometry };
            prim = {
              ( uint32_t )index_start,     ( uint32_t )index_count, ( uint32_t )vertex_start,
              ( uint32_t )meshlets.size(), meshlet_start,
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

  if ( material->pbr_metallic_roughness.base_color_texture.texture )
  {
    cgltf_image const* base_color_image = material->pbr_metallic_roughness.base_color_texture.texture->image;

    if ( not TryLoadTexture( &base_color_texture, *base_color_image, ColorSpaceOverride::kSrgb ) )
    {
      return nullptr;
    }
  }

  if ( material->pbr_metallic_roughness.metallic_roughness_texture.texture )
  {
    cgltf_image const* metal_rough_image = material->pbr_metallic_roughness.metallic_roughness_texture.texture->image;

    if ( not TryLoadTexture( &metal_rough_texture, *metal_rough_image, ColorSpaceOverride::kLinear ) )
    {
      return nullptr;
    }
  }

  if ( material->normal_texture.texture )
  {
    cgltf_image const* normal_image = material->normal_texture.texture->image;

    if ( not TryLoadTexture( &normal_texture, *normal_image, ColorSpaceOverride::kLinear ) )
    {
      return nullptr;
    }
  }

  if ( material->emissive_texture.texture )
  {
    cgltf_image const* emissive_image = material->emissive_texture.texture->image;

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

  MaterialHandle material_handle = m_MaterialManager->CreateMaterialHandle( {
      .BaseColorTexture  = base_color_texture ? base_color_texture.GetSRVHandle() : SRVHandle{},
      .NormalTexture     = normal_texture ? normal_texture.GetSRVHandle() : SRVHandle{},
      .MetalRoughTexture = metal_rough_texture ? metal_rough_texture.GetSRVHandle() : SRVHandle{},
      .EmissiveTexture   = emissive_texture ? emissive_texture.GetSRVHandle() : SRVHandle{},
      .BaseColorFactor   = base_color_factor,
      .EmissiveFactor    = emissive_factor,
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
    RenderDevice*    render_device,
    World*           world,
    TextureLoader*   texture_loader,
    MaterialManager* material_manager,
    GeometryManager* geometry_manager )
  : m_RenderDevice{ render_device }
  , m_World{ world }
  , m_TextureLoader{ texture_loader }
  , m_MaterialManager{ material_manager }
  , m_GeometryManager{ geometry_manager }
{}

std::optional<flecs::entity> Ember::ModelLoader::TryLoadModel( char const* filename )
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

    return {};
  }

  result = cgltf_validate( gltf_model );

  if ( result != cgltf_result_success )
  {
    char buf[512];
    OutputDebugStringA( buf );
    cgltf_free( gltf_model );

    return {};
  }

  result = cgltf_load_buffers( &options, gltf_model, filename );

  if ( result != cgltf_result_success )
  {
    char buf[512];
    sprintf_s( buf, "%s buffers failed to load.", filename );
    OutputDebugStringA( buf );
    cgltf_free( gltf_model );

    return {};
  }

  auto entity = m_World->GetECS().entity().insert( [&]( LocalTransform&, WorldTransform&, WorldBoundingBox& ) {} );

  LoadingContext     context       = { .Geometry = World::GeometryManager().Construct() };

  cgltf_scene const* current_scene = gltf_model->scene;
  for ( uint32_t node_idx = 0; node_idx < current_scene->nodes_count; ++node_idx )
  {
    ProcessNode( &context, entity, *current_scene->nodes[node_idx] );
  }

  GeometryAllocation geom;
  uint32_t           vertex_position_offset;
  uint32_t           vertex_data_offset;
  uint32_t           meshlet_offset;
  uint32_t           meshlet_triangle_offset;
  uint32_t           meshlet_vertices_offset;
  {
    uint32_t vertex_position_size       = ByteSizeOf( context.VertexPositions );
    uint32_t vertex_data_size           = ByteSizeOf( context.VertexData );
    uint32_t meshlet_size               = ByteSizeOf( context.Meshlets );
    uint32_t meshlet_triangle_size      = ByteSizeOf( context.MeshletTriangles );
    uint32_t meshlet_vertices_size      = ByteSizeOf( context.MeshletVertices );

    uint32_t vertex_position_alignment  = StrideOf( context.VertexPositions );
    uint32_t vertex_data_alignment      = StrideOf( context.VertexData );
    uint32_t meshlet_alignment          = StrideOf( context.Meshlets );
    uint32_t meshlet_triangle_alignment = 4;
    uint32_t meshlet_vertices_alignment = StrideOf( context.MeshletVertices );

    uint32_t largest_alignment          = 4;
    largest_alignment                   = std::max( largest_alignment, vertex_position_alignment );
    largest_alignment                   = std::max( largest_alignment, vertex_data_alignment );
    largest_alignment                   = std::max( largest_alignment, meshlet_alignment );
    largest_alignment                   = std::max( largest_alignment, meshlet_triangle_alignment );
    largest_alignment                   = std::max( largest_alignment, meshlet_vertices_alignment );

    uint32_t const total_size = vertex_position_size + vertex_data_size + meshlet_size + meshlet_triangle_size +
                                meshlet_vertices_size + largest_alignment * 5;

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

    ASSERT( meshlet_offset % meshlet_alignment == 0 );
    ASSERT( meshlet_triangle_offset % meshlet_triangle_alignment == 0 );
    ASSERT( meshlet_vertices_offset % meshlet_vertices_alignment == 0 );

    for ( auto& meshlet : context.Meshlets )
    {
      meshlet.TriangleOffset += meshlet_triangle_offset;
      meshlet.VertexOffset   += meshlet_vertices_offset / 4;
    }

    geom.Write(
        vertex_position_offset - base_offset_begin,
        ByteSizeOf( context.VertexPositions ),
        DataOf( context.VertexPositions ) );
    geom.Write(
        vertex_data_offset - base_offset_begin, ByteSizeOf( context.VertexData ), DataOf( context.VertexData ) );
    geom.Write( meshlet_offset - base_offset_begin, ByteSizeOf( context.Meshlets ), DataOf( context.Meshlets ) );
    geom.Write(
        meshlet_triangle_offset - base_offset_begin,
        ByteSizeOf( context.MeshletTriangles ),
        DataOf( context.MeshletTriangles ) );
    geom.Write(
        meshlet_vertices_offset - base_offset_begin,
        ByteSizeOf( context.MeshletVertices ),
        DataOf( context.MeshletVertices ) );
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
          mesh.FirstVertex  += vertex_data_offset / sizeof( VertexData );
          mesh.FirstMeshlet += meshlet_offset / sizeof( Meshlet );
        } );

    ent.children( [&]( flecs::entity child ) { bfs_subtree.push( child ); } );
  }

  context.Geometry->GeometryAlloc = std::move( geom );

  cgltf_free( gltf_model );
  World::GeometryManager().Destroy( context.Geometry );

  Context::Receipt receipt = m_TextureLoader->EndBatch();
  m_RenderDevice->WaitOn( receipt );

  return entity;
}
