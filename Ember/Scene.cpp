#include "Scene.hpp"

#include <imgui.h>

#include "Inspector.hpp"
#include "Util/Profiling.hpp"

#include "Material.hpp"
#include "RenderDevice.hpp"
#include "Util/DataUtil.hpp"

DirectX::XMMATRIX Ember::LocalTransform::GetTransform() const
{
  return DirectX::XMMatrixAffineTransformation(
      XMLoadFloat3( &Scale ), DirectX::XMVectorZero(), XMLoadFloat4( &Rotation ), XMLoadFloat3( &Translation ) );
}

void Ember::LocalTransform::SetTransform( DirectX::FXMMATRIX& transform )
{
  DirectX::XMVECTOR translation, rotation, scale;
  XMMatrixDecompose( &scale, &rotation, &translation, transform );

  XMStoreFloat3( &Translation, translation );
  XMStoreFloat4( &Rotation, rotation );
  XMStoreFloat3( &Scale, scale );
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

struct Quaternion
{
  DirectX::XMFLOAT4 Inner;
};

struct Euler
{
  DirectX::XMFLOAT3 Inner;
};

Ember::World::World()
{
  m_Ecs.import <flecs::stats>();
  m_Ecs.set<flecs::Rest>( {} );

  _ = m_Ecs.component<DirectX::XMFLOAT3>()
          .member<float>( "x", 0, offsetof( DirectX::XMFLOAT3, x ) )
          .member<float>( "y", 0, offsetof( DirectX::XMFLOAT3, y ) )
          .member<float>( "z", 0, offsetof( DirectX::XMFLOAT3, z ) )
          .set<InspectorView>( InspectorView{ []( char const* label, void* elem )
                                              {
                                                DirectX::XMFLOAT3* vec = ( DirectX::XMFLOAT3* )elem;
                                                ImGui::DragFloat3( label, ( float* )vec );
                                              } } );
  _ = m_Ecs.component<Quaternion>()
          .member<float>( "x", 0, offsetof( DirectX::XMFLOAT4, x ) )
          .member<float>( "y", 0, offsetof( DirectX::XMFLOAT4, y ) )
          .member<float>( "z", 0, offsetof( DirectX::XMFLOAT4, z ) )
          .member<float>( "w", 0, offsetof( DirectX::XMFLOAT4, w ) )
          .set<InspectorView>( InspectorView{
              []( char const* label, void* elem )
              {
                // TODO: Avoid all this by Caching the Euler angles.

                DirectX::XMFLOAT4* q  = ( DirectX::XMFLOAT4* )elem;
                q->x                  = -q->x;
                DirectX::XMMATRIX mat = DirectX::XMMatrixRotationQuaternion( XMLoadFloat4( q ) );

                float             euler[3];
                euler[0] = -std::asin( mat.r[1].m128_f32[2] );                        // Pitch
                euler[1] = std::atan2( -mat.r[0].m128_f32[2], mat.r[2].m128_f32[2] ); // Yaw
                euler[2] = std::atan2( -mat.r[1].m128_f32[0], mat.r[1].m128_f32[1] ); // Roll

                for ( float& angle : euler )
                {
                  angle = DirectX::XMConvertToDegrees( angle );
                }

                ImGui::DragFloat3( label, euler );

                for ( float& angle : euler )
                {
                  angle = DirectX::XMConvertToRadians( angle );
                }

                XMStoreFloat4( q, DirectX::XMQuaternionRotationRollPitchYaw( euler[0], euler[1], euler[2] ) );
              } } );
  _ = m_Ecs.component<DirectX::XMFLOAT4>()
          .member<float>( "x", 0, offsetof( DirectX::XMFLOAT4, x ) )
          .member<float>( "y", 0, offsetof( DirectX::XMFLOAT4, y ) )
          .member<float>( "z", 0, offsetof( DirectX::XMFLOAT4, z ) )
          .member<float>( "w", 0, offsetof( DirectX::XMFLOAT4, w ) );
  _ = m_Ecs.component<WorldTransform>();
  _ = m_Ecs.component<LocalTransform>()
          .member<DirectX::XMFLOAT3>( "Translation", 0, offsetof( LocalTransform, Translation ) )
          .member<Quaternion>( "Rotation", 0, offsetof( LocalTransform, Rotation ) )
          .member<DirectX::XMFLOAT3>( "Scale", 0, offsetof( LocalTransform, Scale ) );
  _ = m_Ecs.component<WorldBoundingBox>();
  _ = m_Ecs.component<LocalBoundingBox>();

  //
  m_UpdateRootWorldTransformSys = m_Ecs.system<WorldTransform, LocalTransform const>()
                                      .without( flecs::ChildOf )
                                      .each(
                                          []( WorldTransform& wt, LocalTransform const& lt )
                                          {
                                            wt.Transform    = lt.GetTransform();
                                            wt.InvTransform = XMMatrixInverse( nullptr, wt.Transform );
                                          } );

  m_UpdateWorldTransformSys =
      m_Ecs.system<WorldTransform, LocalTransform const, WorldTransform const>().term_at( 2 ).parent().cascade().each(
          []( WorldTransform& wt, LocalTransform const& lt, WorldTransform const& parent_wt )
          {
            wt.Transform    = XMMatrixMultiply( lt.GetTransform(), parent_wt.Transform );
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
