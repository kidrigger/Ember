#pragma once

#include <memory_resource>

#include "Buffer.hpp"
#include "Color.hpp"
#include "ObjectPool.hpp"
#include "Util/DirectXHeaders.hpp"

#include <flecs.h>

#include "GeometryManager.hpp"

namespace Ember
{
class RenderDevice;
class MaterialImpl;

struct LocalTransform
{
  DirectX::XMVECTOR               Translation{ DirectX::XMVectorSet( 0.0f, 0.0f, 0.0f, 1.0f ) };
  DirectX::XMVECTOR               Rotation{ DirectX::XMQuaternionIdentity() };
  DirectX::XMVECTOR               Scale{ DirectX::XMVectorSplatOne() };

  [[nodiscard]] DirectX::XMMATRIX GetTransform() const;
  void                            SetTransform( DirectX::FXMMATRIX const& transform );
};

struct WorldTransform
{
  DirectX::XMMATRIX Transform{ DirectX::XMMatrixIdentity() };
  DirectX::XMMATRIX InvTransform{ DirectX::XMMatrixIdentity() };
};

struct LocalBoundingBox
{
  DirectX::BoundingBox AABB;
};

struct WorldBoundingBox
{
  DirectX::BoundingBox AABB{
    { 0.0f, 0.0f, 0.0f },
    { 0.0f, 0.0f, 0.0f },
  };

  bool IsInit() const;
};

struct CullInfo
{
  uint64_t           CullMask{ 0 };

  [[nodiscard]] bool AreAnyCulled( uint64_t mask ) const;
  [[nodiscard]] bool AreAllCulled( uint64_t mask ) const;
  void               SetCulled( uint64_t mask );
  void               ClearCulled( uint64_t mask );
};

struct GeometryImpl
{
  GeometryAllocation   GeometryAlloc;
  Buffer               ShadowVertexBuffer;
  Buffer               VertexBuffer;
  Buffer               IndexBuffer;
  Buffer               MeshletBuffer;
  Buffer               MeshletIndexBuffer;
  Buffer               MeshletTriangleBuffer;
  std::atomic_uint32_t RefCount{ 1 };

  uint32_t             AddRef();
  uint32_t             Release();
  uint32_t             GetRefCount();
};

class Material
{
  MaterialImpl* m_Impl{ nullptr };

public:
  MaterialImpl* operator->() const;

  Material() = default;
  explicit Material( MaterialImpl* material );
  Material( Material const& other ) = delete;
  Material( Material&& other ) noexcept;
  Material& operator=( Material const& other ) = delete;
  Material& operator=( Material&& other ) noexcept;
  ~Material();
};

class Geometry
{
  GeometryImpl* m_Impl{ nullptr };

public:
  GeometryImpl* operator->() const;

  Geometry() = default;
  explicit Geometry( GeometryImpl* geometry );
  Geometry( Geometry const& other ) = delete;
  Geometry( Geometry&& other ) noexcept;
  Geometry& operator=( Geometry const& other ) = delete;
  Geometry& operator=( Geometry&& other ) noexcept;
  ~Geometry();
};

struct Mesh
{
  uint32_t FirstIndex;
  uint32_t IndexCount;
  uint32_t FirstVertex;
  uint32_t MeshletCount;
  uint32_t FirstMeshlet;
};

struct alignas( 16 ) MeshDraw
{
  uint32_t       FirstTransform;
  uint32_t       TransformCount;
  uint32_t       FirstVertex;
  uint32_t       FirstMeshlet;
  uint32_t       MeshletCount;
  MaterialHandle Material;
  uint32_t       Padding[2];
};

class DrawList
{
  struct FrameResources
  {
    Buffer TransformBuffer;
    Buffer OpaqueDrawBuffer;
    Buffer AlphaTestedDrawBuffer;
    Buffer AlphaBlendedDrawBuffer;
  };

  RenderDevice*               m_RenderDevice;
  GeometryManager*            m_GeometryManager;

  std::vector<WorldTransform> m_Transforms;
  std::vector<MeshDraw>       m_OpaqueDrawInfos;
  std::vector<MeshDraw>       m_AlphaTestedDrawInfos;
  std::vector<MeshDraw>       m_AlphaBlendedDrawInfos;
  std::vector<FrameResources> m_FrameResources;

public:
  struct Info
  {
    SRVHandle Transforms;
    SRVHandle DrawInfos;
    uint32_t  DrawCount;
    SRVHandle GeometryHandle;
  };

  struct Batches
  {
    Info Opaque;
    Info AlphaTested;
    Info AlphaBlended;
  };

  DrawList( RenderDevice* render_device, GeometryManager* geometry_manager, uint32_t frame_count );

  void                  PushDraw( WorldTransform const& transform, Mesh const& mesh, Material const& material );
  [[nodiscard]] Batches PrepareFrame( uint32_t frame_idx );
  void                  Clear();
  [[nodiscard]] size_t  GetOpaqueCount() const;
  [[nodiscard]] size_t  GetAlphaTestedCount() const;
  [[nodiscard]] size_t  GetAlphaBlendedCount() const;
  [[nodiscard]] size_t  GetTotalCount() const;
};

class World
{
  flecs::world                                                                 m_Ecs;
  flecs::query<WorldTransform, LocalTransform const>                           m_UpdateRootWorldTransformQuery;
  flecs::query<WorldTransform, LocalTransform const, WorldTransform const>     m_UpdateWorldTransformQuery;
  flecs::query<WorldBoundingBox, LocalBoundingBox const, WorldTransform const> m_PrimeActualWorldAABBQuery;
  flecs::query<WorldBoundingBox>                                               m_PrimeCollectingWorldAABBQuery;
  flecs::query<WorldBoundingBox, WorldBoundingBox const>                       m_UpdateWorldAABBQuery;
  flecs::query<CullInfo, WorldBoundingBox const, CullInfo const>               m_CullDescentQuery;
  flecs::query<WorldTransform const, CullInfo const, Mesh const, Material const, Geometry const> m_RenderQuery;

public:
  static ObjectPool<GeometryImpl>& GeometryManager();
  static ObjectPool<MaterialImpl>& MaterialManager();

  World();

  void Update( float delta_seconds ) const;
  void ClearCull( uint64_t cull_mask = UINT64_MAX ) const;
  void CullFrustum( DirectX::BoundingFrustum const& frustum ) const;
  void CullSphere( DirectX::BoundingSphere const& sphere ) const;
  void CullBox( DirectX::BoundingOrientedBox const& bob, uint64_t cull_mask ) const;

  //
  flecs::world const& GetECS() const;
};

} // namespace Ember
