#pragma once

#include <memory_resource>

#include "Buffer.hpp"
#include "Color.hpp"
#include "ObjectPool.hpp"
#include "Util/DirectXHeaders.hpp"

#include <flecs.h>

namespace Ember
{
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
  Buffer               ShadowVertexBuffer;
  Buffer               VertexBuffer;
  Buffer               IndexBuffer;
  Buffer               MeshletBuffer;
  Buffer               MeshletVerticesBuffer;
  Buffer               MeshletTrianglesBuffer;
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
