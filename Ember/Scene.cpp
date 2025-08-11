#include "Scene.hpp"

#include "Util/Profiling.hpp"

#include "DebugInfo.hpp"

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

bool Ember::CullInfo::IsCulled( uint64_t const mask ) const
{
  return CullMask & mask;
}

void Ember::CullInfo::SetCulled( uint64_t const mask )
{
  CullMask |= mask;
}

uint32_t Ember::Material::AddRef()
{
  return ++RefCount;
}

uint32_t Ember::Material::Release()
{
  return --RefCount;
}

uint32_t Ember::Material::GetRefCount()
{
  return RefCount;
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

Ember::Primitive::Primitive( Ember::Material* const material, DirectX::BoundingBox aabb, Data draw_info )
  : Material{ material }, AABB{ std::move( aabb ) }, DrawInfo{ std::move( draw_info ) }
{}

Ember::Primitive::Primitive( Primitive&& other ) noexcept
  : Material{ other.Material }, AABB{ std::move( other.AABB ) }, DrawInfo{ std::move( other.DrawInfo ) }
{
  other.Material = nullptr;
}

Ember::Primitive& Ember::Primitive::operator=( Primitive&& other ) noexcept
{
  if ( this == &other ) return *this;
  World::MaterialManager().Destroy( Material );
  Material       = other.Material;
  other.Material = nullptr;
  AABB           = std::move( other.AABB );
  DrawInfo       = std::move( other.DrawInfo );
  return *this;
}

Ember::Primitive::~Primitive()
{
  World::MaterialManager().Destroy( Material );
}

Ember::Mesh::Mesh( std::vector<Primitive> primitives, Ember::Geometry* const geometry )
  : Primitives{ std::move( primitives ) }, Geometry{ geometry }
{}

Ember::Mesh::Mesh( Mesh&& other ) noexcept : Primitives{ std::move( other.Primitives ) }, Geometry{ other.Geometry }
{
  other.Geometry = nullptr;
}

Ember::Mesh& Ember::Mesh::operator=( Mesh&& other ) noexcept
{
  if ( this == &other ) return *this;
  Primitives = std::move( other.Primitives );
  World::GeometryManager().Destroy( Geometry );
  Geometry       = other.Geometry;
  other.Geometry = nullptr;
  return *this;
}

Ember::Mesh::~Mesh()
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
}

void Ember::World::Update( float delta_seconds ) const
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

void Ember::World::Render( ID3D12GraphicsCommandList* command_list, DirectX::BoundingFrustum const& frustum ) const
{
  ZoneScoped;

  // Clear cull flags;
  m_Ecs.each( []( CullInfo& cull_info ) { cull_info.SetCulled( 0 ); } );

  uint64_t const cull_mask = 0x1;
  {
    ZoneScopedN( "Cull" );
    m_CullDescentQuery.each(
        [&]( CullInfo& cull_info, WorldBoundingBox const& bb, CullInfo const& parent_cull )
        {
          cull_info.SetCulled( parent_cull.CullMask );
          if ( cull_info.IsCulled( cull_mask ) ) return;

          if ( frustum.Contains( bb.AABB ) == DirectX::DISJOINT or bb.AABB.Contains( frustum ) == DirectX::DISJOINT )
          {
            cull_info.SetCulled( cull_mask );
          }
        } );
  }

  {
    ZoneScopedN( "RecordCmdList" );

    m_Ecs.each(
        [&]( WorldTransform const& wt, Mesh const& mesh )
        {
          for ( Primitive const& primitive : mesh.Primitives )
          {
            DirectX::BoundingBox bb;
            primitive.AABB.Transform( bb, wt.Transform );
            if ( frustum.Contains( bb ) == DirectX::DISJOINT )
            {
              continue;
            }

            command_list->IASetIndexBuffer( &mesh.Geometry->IndexBuffer.GetIndexBufferView() );
            command_list->IASetVertexBuffers( 0, 1, &mesh.Geometry->VertexBuffer.GetVertexBufferView() );

            command_list->SetGraphicsRoot32BitConstants( 0, sizeof( WorldTransform ) / 4, &wt, 0 );
            command_list->SetGraphicsRoot32BitConstants(
                1, sizeof( Material::GpuRepr ) / 4, &primitive.Material->Repr, 0 );

            DebugInfo::Instance().PushDrawCall( primitive.DrawInfo.IndexCount );
            command_list->DrawIndexedInstanced(
                primitive.DrawInfo.IndexCount, 1, primitive.DrawInfo.FirstIndex, primitive.DrawInfo.FirstVertex, 0 );
          }
        } );
  }
}

void Ember::World::RenderShadow(
    ID3D12GraphicsCommandList* command_list, DirectX::BoundingFrustum const& frustum ) const
{
  ZoneScoped;

  // Clear cull flags;
  m_Ecs.each( []( CullInfo& cull_info ) { cull_info.SetCulled( 0 ); } );

  uint64_t const cull_mask = 0x1;
  {
    ZoneScopedN( "Cull" );
    m_CullDescentQuery.each(
        [&]( CullInfo& cull_info, WorldBoundingBox const& bb, CullInfo const& parent_cull )
        {
          cull_info.SetCulled( parent_cull.CullMask );
          if ( cull_info.IsCulled( cull_mask ) ) return;

          if ( frustum.Contains( bb.AABB ) == DirectX::DISJOINT or bb.AABB.Contains( frustum ) == DirectX::DISJOINT )
          {
            cull_info.SetCulled( cull_mask );
          }
        } );
  }

  {
    ZoneScopedN( "RecordCmdList" );

    m_Ecs.each(
        [&]( WorldTransform const& wt, Mesh const& mesh )
        {
          for ( Primitive const& primitive : mesh.Primitives )
          {
            DirectX::BoundingBox bb;
            primitive.AABB.Transform( bb, wt.Transform );
            if ( frustum.Contains( bb ) == DirectX::DISJOINT )
            {
              continue;
            }

            command_list->IASetIndexBuffer( &mesh.Geometry->IndexBuffer.GetIndexBufferView() );
            command_list->IASetVertexBuffers( 0, 1, &mesh.Geometry->VertexBuffer.GetVertexBufferView() );

            command_list->SetGraphicsRoot32BitConstants( 0, sizeof( DirectX::XMMATRIX ) / 4, &wt.Transform, 0 );

            DebugInfo::Instance().PushDrawCall( primitive.DrawInfo.IndexCount );
            command_list->DrawIndexedInstanced(
                primitive.DrawInfo.IndexCount, 1, primitive.DrawInfo.FirstIndex, primitive.DrawInfo.FirstVertex, 0 );
          }
        } );
  }
}

flecs::world& Ember::World::GetECS()
{
  return m_Ecs;
}
