#include "Scene.hpp"

#include "Util/Profiling.hpp"

#include "Material.hpp"
#include "RenderDevice.hpp"
#include "Util/DataUtil.hpp"

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

Ember::Geometry::Geometry( Ember::GeometryImpl* const geometry ) : m_Impl{ geometry }
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
  : m_RenderDevice{ render_device }
  , m_GeometryManager{ geometry_manager }
  , m_TransformBuffers{ frame_count }
  , m_DrawBuffers{ frame_count }
{}

void Ember::DrawList::PushDraw(
    WorldTransform const& transform, Mesh const& mesh, Geometry const& geometry, Material const& material )
{
  uint32_t const transform_idx = ( uint32_t )m_Transforms.size();
  m_Transforms.push_back( transform );

  int remaining_meshlets = ( int )mesh.MeshletCount;
  int meshlet_offset     = ( int )mesh.FirstMeshlet;

  while ( remaining_meshlets > 0 )
  {
    m_DrawInfos.emplace_back(
        transform_idx, 1, mesh.FirstVertex, meshlet_offset, std::min( remaining_meshlets, 32 ), material->GetHandle() );

    remaining_meshlets -= 32;
    meshlet_offset     += 32;
  }
}

Ember::DrawList::Info Ember::DrawList::PrepareFrame( uint32_t const frame_idx )
{
  uint32_t const transform_size   = ByteSizeOf( m_Transforms );
  Buffer*        transform_buffer = &m_TransformBuffers[frame_idx];

  if ( transform_buffer->GetSize() < transform_size )
  {
    *transform_buffer = m_RenderDevice->CreateStorageBuffer( transform_size, StrideOf( m_Transforms ) );
  }

  uint32_t const draw_size   = ByteSizeOf( m_DrawInfos );
  Buffer*        draw_buffer = &m_DrawBuffers[frame_idx];

  if ( draw_buffer->GetSize() < draw_size )
  {
    *draw_buffer = m_RenderDevice->CreateStorageBuffer( draw_size, StrideOf( m_DrawInfos ) );
  }

  transform_buffer->Write( 0, transform_size, DataOf( m_Transforms ) );
  draw_buffer->Write( 0, draw_size, DataOf( m_DrawInfos ) );

  return {
    transform_buffer->GetSRVHandle(),
    draw_buffer->GetSRVHandle(),
    CountOf( m_DrawInfos ),
    m_GeometryManager->GetSRVHandle(),
  };
}

void Ember::DrawList::Clear()
{
  m_Transforms.clear();
  m_DrawInfos.clear();
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
      m_Ecs.query_builder<WorldTransform const, CullInfo const, Mesh const, Material const, Geometry const>().build();
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
