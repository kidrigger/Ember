#include "Scene.hpp"

#include "Util/Profiling.hpp"

#include "Material.hpp"

DirectX::XMMATRIX Ember::LocalTransform::GetTransform() const
{
  return DirectX::XMMatrixAffineTransformation( Scale, DirectX::XMVectorZero(), Rotation, Translation );
}

void Ember::LocalTransform::SetTransform( DirectX::FXMMATRIX& transform )
{
  XMMatrixDecompose( &Scale, &Rotation, &Translation, transform );
}

bool Ember::WorldBoundingBox::IsInit() const
{
  return AABB.Extents.x != 0.0f or AABB.Extents.y != 0.0f or AABB.Extents.y != 0.0f;
}

bool Ember::CullInfo::AreAnyCulled( uint64_t const mask ) const
{
  return CullMask & mask;
}

bool Ember::CullInfo::AreAllCulled( uint64_t const mask ) const
{
  return ( CullMask & mask ) == mask;
}

void Ember::CullInfo::SetCulled( uint64_t const mask )
{
  CullMask |= mask;
}

void Ember::CullInfo::ClearCulled( uint64_t const mask )
{
  CullMask &= ~mask;
}

uint32_t Ember::Geometry::AddRef()
{
  return ++RefCount;
}

uint32_t Ember::Geometry::Release()
{
  return --RefCount;
}

uint32_t Ember::Geometry::GetRefCount()
{
  return RefCount;
}

Ember::MaterialComp::MaterialComp( Ember::Material* const material ) : Material{ material }
{}

Ember::MaterialComp::MaterialComp( MaterialComp&& other ) noexcept : Material{ other.Material }
{
  other.Material = nullptr;
}

Ember::MaterialComp& Ember::MaterialComp::operator=( MaterialComp&& other ) noexcept
{
  if ( this == &other ) return *this;

  World::MaterialManager().Destroy( Material );
  Material       = other.Material;
  other.Material = nullptr;

  return *this;
}

Ember::MaterialComp::~MaterialComp()
{
  World::MaterialManager().Destroy( Material );
}

Ember::GeometryComp::GeometryComp( Ember::Geometry* const geometry ) : Geometry{ geometry }
{}

Ember::GeometryComp::GeometryComp( GeometryComp&& other ) noexcept : Geometry{ other.Geometry }
{
  other.Geometry = nullptr;
}

Ember::GeometryComp& Ember::GeometryComp::operator=( GeometryComp&& other ) noexcept
{
  if ( this == &other ) return *this;

  World::GeometryManager().Destroy( Geometry );
  Geometry       = other.Geometry;
  other.Geometry = nullptr;

  return *this;
}

Ember::GeometryComp::~GeometryComp()
{
  World::GeometryManager().Destroy( Geometry );
}

Ember::ObjectPool<Ember::Geometry>& Ember::World::GeometryManager()
{
  static ObjectPool<Geometry> manager;
  return manager;
}

Ember::ObjectPool<Ember::Material>& Ember::World::MaterialManager()
{
  static ObjectPool<Material> manager;
  return manager;
}

Ember::World::World()
{
  m_UpdateRootWorldTransformQuery =
      m_Ecs.query_builder<WorldTransform, LocalTransform const>().without( flecs::ChildOf ).build();

  m_UpdateWorldTransformQuery = m_Ecs.query_builder<WorldTransform, LocalTransform const, WorldTransform const>()
                                    .term_at( 2 )
                                    .parent()
                                    .cascade()
                                    .build();

  m_PrimeActualWorldAABBQuery =
      m_Ecs.query_builder<WorldBoundingBox, LocalBoundingBox const, WorldTransform const>().build();

  m_PrimeCollectingWorldAABBQuery = m_Ecs.query_builder<WorldBoundingBox>().without<LocalBoundingBox>().build();

  m_UpdateWorldAABBQuery =
      m_Ecs.query_builder<WorldBoundingBox, WorldBoundingBox const>().term_at( 0 ).parent().cascade().desc().build();

  m_CullDescentQuery =
      m_Ecs.query_builder<CullInfo, WorldBoundingBox const, CullInfo const>().term_at( 2 ).parent().cascade().build();

  m_RenderQuery =
      m_Ecs.query_builder<WorldTransform const, CullInfo const, Mesh const, MaterialComp const, GeometryComp const>()
          .build();
}

void Ember::World::Update( float ) const
{
  ZoneScoped;

  {
    ZoneScopedN( "UpdateWorldTransforms" );
    m_UpdateRootWorldTransformQuery.each(
        []( WorldTransform& wt, LocalTransform const& lt )
        {
          wt.Transform    = lt.GetTransform();
          wt.InvTransform = XMMatrixInverse( nullptr, wt.Transform );
        } );
    m_UpdateWorldTransformQuery.each(
        []( WorldTransform& wt, LocalTransform const& lt, WorldTransform const& parent_wt )
        {
          wt.Transform    = XMMatrixMultiply( lt.GetTransform(), parent_wt.Transform );
          wt.InvTransform = XMMatrixInverse( nullptr, wt.Transform );
        } );
  }

  {
    ZoneScopedN( "PrimeWorldBoundingBoxes" );
    m_PrimeCollectingWorldAABBQuery.each( []( WorldBoundingBox& wbb ) { wbb = {}; } );
    m_PrimeActualWorldAABBQuery.each( []( WorldBoundingBox& wbb, LocalBoundingBox const& lbb, WorldTransform const& wt )
                                      { lbb.AABB.Transform( wbb.AABB, wt.Transform ); } );
  }

  {
    ZoneScopedN( "UpdateWorldAABBQuery" );
    m_UpdateWorldAABBQuery.each(
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
}

void Ember::World::ClearCull( uint64_t const cull_mask ) const
{
  m_Ecs.each( [&]( CullInfo& cull_info ) { cull_info.ClearCulled( cull_mask ); } );
}

void Ember::World::CullFrustum( DirectX::BoundingFrustum const& frustum ) const
{
  ZoneScoped;

  // Clear cull flags;
  uint64_t const cull_mask = UINT64_MAX;
  ClearCull( cull_mask );

  m_CullDescentQuery.each(
      [&]( CullInfo& cull_info, WorldBoundingBox const& bb, CullInfo const& parent_cull )
      {
        cull_info.SetCulled( parent_cull.CullMask );
        if ( cull_info.AreAnyCulled( cull_mask ) ) return;

        if ( frustum.Contains( bb.AABB ) == DirectX::DISJOINT or bb.AABB.Contains( frustum ) == DirectX::DISJOINT )
        {
          cull_info.SetCulled( cull_mask );
        }
      } );
}

void Ember::World::CullSphere( DirectX::BoundingSphere const& sphere ) const
{
  ZoneScoped;

  // Clear cull flags;
  uint64_t const cull_mask = UINT64_MAX;
  ClearCull( cull_mask );

  m_CullDescentQuery.each(
      [&]( CullInfo& cull_info, WorldBoundingBox const& bb, CullInfo const& parent_cull )
      {
        cull_info.SetCulled( parent_cull.CullMask );
        if ( cull_info.AreAllCulled( cull_mask ) ) return;

        if ( sphere.Contains( bb.AABB ) == DirectX::DISJOINT )
        {
          cull_info.SetCulled( cull_mask );
        }
      } );
}

void Ember::World::CullBox( DirectX::BoundingOrientedBox const& bob, uint64_t const cull_mask ) const
{
  ZoneScoped;

  // Clear cull flags;
  ClearCull( cull_mask );

  m_CullDescentQuery.each(
      [&]( CullInfo& cull_info, WorldBoundingBox const& bb, CullInfo const& parent_cull )
      {
        cull_info.SetCulled( parent_cull.CullMask );
        if ( cull_info.AreAllCulled( cull_mask ) ) return;

        if ( bob.Contains( bb.AABB ) == DirectX::DISJOINT )
        {
          cull_info.SetCulled( cull_mask );
        }
      } );
}

flecs::world const& Ember::World::GetECS() const
{
  return m_Ecs;
}
