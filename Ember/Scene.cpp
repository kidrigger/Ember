#include "Scene.hpp"

#include <imgui.h>

#include "Inspector.hpp"
#include "Util/Profiling.hpp"

#include "Material.hpp"
#include "RenderDevice.hpp"
#include "Util/DataUtil.hpp"

void Ember::TransformUtil::DecomposeMatrix(
    Scale* out_scale, Rotation* out_rotation, Translation* out_translation, DirectX::XMMATRIX const& matrix )
{
  ASSERT( out_scale );
  ASSERT( out_rotation );
  ASSERT( out_translation );

  DirectX::XMVECTOR scale, rotation, translation;
  ENSURE( XMMatrixDecompose( &scale, &rotation, &translation, matrix ) );

  XMStoreFloat3( &out_scale->Value, scale );
  XMStoreFloat4( &out_rotation->Value, rotation );
  XMStoreFloat3( &out_translation->Value, translation );
}

void Ember::TransformUtil::DecomposeMatrixPartial(
    Scale* out_scale, Rotation* out_rotation, Translation* out_translation, DirectX::XMMATRIX const& matrix )
{
  DirectX::XMVECTOR scale, rotation, translation;
  ENSURE( XMMatrixDecompose( &scale, &rotation, &translation, matrix ) );

  if ( out_scale ) XMStoreFloat3( &out_scale->Value, scale );
  if ( out_rotation ) XMStoreFloat4( &out_rotation->Value, rotation );
  if ( out_translation ) XMStoreFloat3( &out_translation->Value, translation );
}

DirectX::XMMATRIX Ember::TransformUtil::ConstructMatrix(
    Scale const& scale, Rotation const& rotation, Translation const& translation )
{
  return DirectX::XMMatrixAffineTransformation(
      scale.ToVector(), DirectX::XMVectorZero(), rotation.ToVector(), translation.ToVector() );
}

DirectX::XMMATRIX Ember::TransformUtil::ConstructMatrixPartial(
    Scale const* scale, Rotation const* rotation, Translation const* translation )
{
  DirectX::XMVECTOR const v_scale       = scale ? scale->ToVector() : DirectX::XMVectorSplatOne();
  DirectX::XMVECTOR const v_rotation    = rotation ? rotation->ToVector() : DirectX::XMQuaternionIdentity();
  DirectX::XMVECTOR const v_translation = translation ? translation->ToVector() : DirectX::XMVectorZero();

  return DirectX::XMMatrixAffineTransformation( v_scale, DirectX::XMVectorZero(), v_rotation, v_translation );
}

DirectX::XMFLOAT3 Ember::WorldTransform::GetTranslation() const
{
  DirectX::XMFLOAT3 translation;
  XMStoreFloat3( &translation, Transform.r[3] );
  return translation;
}

bool Ember::WorldBoundingBox::IsInit() const
{
  return AABB.Extents.x != 0.0f or AABB.Extents.y != 0.0f or AABB.Extents.y != 0.0f;
}

uint32_t Ember::GeometryImpl::AddRef()
{
  return ++RefCount;
}

uint32_t Ember::GeometryImpl::Release()
{
  return --RefCount;
}

uint32_t Ember::GeometryImpl::GetRefCount()
{
  return RefCount;
}

Ember::MaterialImpl* Ember::Material::operator->() const
{
  return m_Impl;
}

Ember::Material::Material( Ember::MaterialImpl* const material ) : m_Impl{ material }
{}

Ember::Material::Material( Material&& other ) noexcept : m_Impl{ other.m_Impl }
{
  other.m_Impl = nullptr;
}

Ember::Material& Ember::Material::operator=( Material&& other ) noexcept
{
  if ( this == &other ) return *this;

  World::MaterialManager().Destroy( m_Impl );
  m_Impl       = other.m_Impl;
  other.m_Impl = nullptr;

  return *this;
}

Ember::Material::~Material()
{
  World::MaterialManager().Destroy( m_Impl );
}

Ember::GeometryImpl* Ember::Geometry::operator->() const
{
  return m_Impl;
}

Ember::Geometry::Geometry( GeometryImpl* const geometry ) : m_Impl{ geometry }
{}

Ember::Geometry::Geometry( Geometry&& other ) noexcept : m_Impl{ other.m_Impl }
{
  other.m_Impl = nullptr;
}

Ember::Geometry& Ember::Geometry::operator=( Geometry&& other ) noexcept
{
  if ( this == &other ) return *this;

  World::GeometryManager().Destroy( m_Impl );
  m_Impl       = other.m_Impl;
  other.m_Impl = nullptr;

  return *this;
}

Ember::Geometry::~Geometry()
{
  World::GeometryManager().Destroy( m_Impl );
}

Ember::ObjectPool<Ember::GeometryImpl>& Ember::World::GeometryManager()
{
  static ObjectPool<GeometryImpl> manager;
  return manager;
}

Ember::ObjectPool<Ember::MaterialImpl>& Ember::World::MaterialManager()
{
  static ObjectPool<MaterialImpl> manager;
  return manager;
}

Ember::DrawList::DrawList( RenderDevice* render_device, GeometryManager* geometry_manager, uint32_t const frame_count )
  : m_RenderDevice{ render_device }, m_GeometryManager{ geometry_manager }, m_FrameResources{ frame_count }
{}

void Ember::DrawList::PushDraw( WorldTransform const& transform, Mesh const& mesh, Material const& material )
{
  std::vector<MeshDraw>* draw_infos;
  switch ( material->GetAlphaMode() )
  {
    case AlphaMode::kOpaque:
      draw_infos = &m_OpaqueDrawInfos;
      break;
    case AlphaMode::kMask:
      draw_infos = &m_AlphaTestedDrawInfos;
      break;
    case AlphaMode::kBlend:
      draw_infos = &m_AlphaBlendedDrawInfos;
      break;
    default:
      UNREACHABLE;
  }

  uint32_t const transform_idx = ( uint32_t )m_Transforms.size();
  m_Transforms.push_back( transform );

  int remaining_meshlets = ( int )mesh.MeshletCount;
  int meshlet_offset     = ( int )mesh.FirstMeshlet;

  while ( remaining_meshlets > 0 )
  {
    draw_infos->emplace_back(
        transform_idx, 1, mesh.FirstVertex, meshlet_offset, std::min( remaining_meshlets, 32 ), material->GetHandle() );

    remaining_meshlets -= 32;
    meshlet_offset     += 32;
  }
}

namespace
{

void ResizedWrite(
    Ember::RenderDevice* render_device, Ember::Buffer* buffer, std::ranges::contiguous_range auto const& draws )
{
  uint32_t const draw_size = ByteSizeOf( draws );

  if ( buffer->GetSize() < draw_size )
  {
    *buffer = render_device->CreateStorageBuffer( draw_size, StrideOf( draws ) );
  }
  buffer->Write( 0, draw_size, DataOf( draws ) );
}
} // namespace

Ember::DrawList::Batches Ember::DrawList::PrepareFrame( uint32_t const frame_idx )
{
  FrameResources& resources = m_FrameResources[frame_idx];

  ResizedWrite( m_RenderDevice, &resources.TransformBuffer, m_Transforms );
  ResizedWrite( m_RenderDevice, &resources.OpaqueDrawBuffer, m_OpaqueDrawInfos );
  ResizedWrite( m_RenderDevice, &resources.AlphaTestedDrawBuffer, m_AlphaTestedDrawInfos );
  ResizedWrite( m_RenderDevice, &resources.AlphaBlendedDrawBuffer, m_AlphaBlendedDrawInfos );

  return {
    .Opaque = {
      resources.TransformBuffer.GetSRVHandle(),
      resources.OpaqueDrawBuffer.GetSRVHandle(),
      CountOf( m_OpaqueDrawInfos ),
      m_GeometryManager->GetSRVHandle(),
    },
    .AlphaTested = {
      resources.TransformBuffer.GetSRVHandle(),
      resources.AlphaTestedDrawBuffer.GetSRVHandle(),
      CountOf( m_AlphaTestedDrawInfos ),
      m_GeometryManager->GetSRVHandle(),
    },
    .AlphaBlended = {
      resources.TransformBuffer.GetSRVHandle(),
      resources.AlphaBlendedDrawBuffer.GetSRVHandle(),
      CountOf( m_AlphaBlendedDrawInfos ),
      m_GeometryManager->GetSRVHandle(),
    }
  };
}

void Ember::DrawList::Clear()
{
  m_Transforms.clear();
  m_OpaqueDrawInfos.clear();
  m_AlphaTestedDrawInfos.clear();
  m_AlphaBlendedDrawInfos.clear();
}

size_t Ember::DrawList::GetOpaqueCount() const
{
  return m_OpaqueDrawInfos.size();
}

size_t Ember::DrawList::GetAlphaTestedCount() const
{
  return m_AlphaTestedDrawInfos.size();
}

size_t Ember::DrawList::GetAlphaBlendedCount() const
{
  return m_AlphaBlendedDrawInfos.size();
}

size_t Ember::DrawList::GetTotalCount() const
{
  return m_OpaqueDrawInfos.size() + m_AlphaBlendedDrawInfos.size() + m_AlphaTestedDrawInfos.size();
}

Ember::World::World()
{
  m_Ecs.import <flecs::stats>();
  m_Ecs.set<flecs::Rest>( {} );

  flecs::entity world_transform = m_Ecs.component<WorldTransform>();
  flecs::entity wbb             = m_Ecs.component<WorldBoundingBox>().add( flecs::With, world_transform );
  _                             = m_Ecs.component<LocalBoundingBox>().add( flecs::With, wbb );

  _                             = m_Ecs.component<Translation>()
          .member<float>( "x", 0, offsetof( DirectX::XMFLOAT3, x ) )
          .member<float>( "y", 0, offsetof( DirectX::XMFLOAT3, y ) )
          .member<float>( "z", 0, offsetof( DirectX::XMFLOAT3, z ) )
          .add( flecs::With, world_transform )
          .set<InspectorView>( InspectorView{ []( char const* label, void* elem )
                                              {
                                                DirectX::XMFLOAT3* vec = ( DirectX::XMFLOAT3* )elem;
                                                ImGui::DragFloat3( label ? label : "Value", ( float* )vec );
                                              } } );

  _ = m_Ecs.component<Rotation>()
          .member<float>( "x", 0, offsetof( DirectX::XMFLOAT4, x ) )
          .member<float>( "y", 0, offsetof( DirectX::XMFLOAT4, y ) )
          .member<float>( "z", 0, offsetof( DirectX::XMFLOAT4, z ) )
          .member<float>( "w", 0, offsetof( DirectX::XMFLOAT4, w ) )
          .add( flecs::With, world_transform )
          .set<InspectorView>( InspectorView{ []( char const* label, void* elem )
                                              {
                                                // TODO: Avoid all this by Caching the Euler angles.

                                                DirectX::XMFLOAT4* q     = ( DirectX::XMFLOAT4* )elem;
                                                DirectX::XMFLOAT3  euler = Quaternion::ToEuler( *q );

                                                ImGui::PushID( label ? label : "Rotation" );
                                                ImGui::SliderAngle( "Pitch", &euler.x, -89.0f, 89.0f );
                                                ImGui::SliderAngle( "Yaw", &euler.y, -180.0f, 180.0f );
                                                ImGui::SliderAngle( "Roll", &euler.z, -180.0f, 180.0f );
                                                ImGui::PopID();

                                                *q = Quaternion::FromEuler( euler );
                                              } } );

  _ = m_Ecs.component<Scale>()
          .member<float>( "x", 0, offsetof( DirectX::XMFLOAT3, x ) )
          .member<float>( "y", 0, offsetof( DirectX::XMFLOAT3, y ) )
          .member<float>( "z", 0, offsetof( DirectX::XMFLOAT3, z ) )
          .add( flecs::With, world_transform )
          .set<InspectorView>( InspectorView{ []( char const* label, void* elem )
                                              {
                                                DirectX::XMFLOAT3* vec = ( DirectX::XMFLOAT3* )elem;
                                                ImGui::DragFloat3( label ? label : "Value", ( float* )vec );
                                              } } );

  //
  m_UpdateRootWorldTransformSys =
      m_Ecs.system<WorldTransform, Translation const*, Rotation const*, Scale const*>()
          .without( flecs::ChildOf )
          .each(
              []( WorldTransform& wt, Translation const* translation, Rotation const* rotation, Scale const* scale )
              {
                wt.Transform    = TransformUtil::ConstructMatrixPartial( scale, rotation, translation );
                wt.InvTransform = XMMatrixInverse( nullptr, wt.Transform );
              } );

  m_UpdateWorldTransformSys =
      m_Ecs.system<WorldTransform, Translation const*, Rotation const*, Scale const*, WorldTransform const>()
          .term_at( 4 )
          .parent()
          .cascade()
          .each(
              []( WorldTransform& wt,
                  Translation const* translation,
                  Rotation const* rotation,
                  Scale const* scale,
                  WorldTransform const& parent_wt )
              {
                wt.Transform = XMMatrixMultiply(
                    TransformUtil::ConstructMatrixPartial( scale, rotation, translation ), parent_wt.Transform );

                wt.InvTransform = XMMatrixInverse( nullptr, wt.Transform );
              } );

  m_PrimeCollectingWorldAABBSys =
      m_Ecs.system<WorldBoundingBox>().without<LocalBoundingBox>().each( []( WorldBoundingBox& wbb ) { wbb = {}; } );

  m_PrimeActualWorldAABBSys = m_Ecs.system<WorldBoundingBox, LocalBoundingBox const, WorldTransform const>().each(
      []( WorldBoundingBox& wbb, LocalBoundingBox const& lbb, WorldTransform const& wt )
      { lbb.AABB.Transform( wbb.AABB, wt.Transform ); } );

  m_UpdateWorldAABBSys =
      m_Ecs.system<WorldBoundingBox, WorldBoundingBox const>().term_at( 0 ).parent().cascade().desc().each(
          []( WorldBoundingBox& parent_bb, WorldBoundingBox const& bb )
          {
            if ( parent_bb.IsInit() )
            {
              DirectX::BoundingBox::CreateMerged( parent_bb.AABB, parent_bb.AABB, bb.AABB );
            }
            else
            {
              parent_bb.AABB = bb.AABB;
            }
          } );
}

void Ember::World::Update( float const delta_time ) const
{
  ZoneScoped;

  {
    ZoneScopedN( "UpdateWorldTransforms" );
    m_UpdateRootWorldTransformSys.run( delta_time );
    m_UpdateWorldTransformSys.run( delta_time );
  }

  {
    ZoneScopedN( "PrimeWorldBoundingBoxes" );
    m_PrimeCollectingWorldAABBSys.run( delta_time );
    m_PrimeActualWorldAABBSys.run( delta_time );
  }

  {
    ZoneScopedN( "UpdateWorldAABBQuery" );
    m_UpdateWorldAABBSys.run( delta_time );
  }
}

flecs::world const& Ember::World::GetECS() const
{
  return m_Ecs;
}

flecs::world& Ember::World::GetECS()
{
  return m_Ecs;
}
