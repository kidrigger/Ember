#pragma once

#include <memory_resource>

#include "Buffer.hpp"
#include "Color.hpp"
#include "ObjectPool.hpp"
#include "Texture.hpp"
#include "Util/DirectXHeaders.hpp"

#include <flecs.h>

namespace Ember
{

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

struct Material
{
  struct alignas( 16 ) GpuRepr
  {
    SRVHandle BaseColorTexture;  // 04
    SRVHandle NormalTexture;     // 08
    SRVHandle MetalRoughTexture; // 12
    SRVHandle EmissiveTexture;   // 16
    Color32   BaseColorFactor;   // 20
    Color32   EmissiveFactor;    // 24
    float     EmissiveStrength;  // 28
    float     Metal;             // 32
    float     Rough;             // 36
    float     AlphaCutoff;       // 40
    uint32_t  Padding0;          // 44
    uint32_t  Padding1;          // 48
  };

  Texture              BaseColorTexture;
  Texture              NormalTexture;
  Texture              MetalRoughTexture;
  Texture              EmissiveTexture;
  GpuRepr              Repr;
  std::atomic_uint32_t RefCount{ 1 };

  uint32_t             AddRef();
  uint32_t             Release();
  uint32_t             GetRefCount();
};

struct Geometry
{
  Buffer               ShadowVertexBuffer;
  Buffer               VertexBuffer;
  Buffer               IndexBuffer;
  std::atomic_uint32_t RefCount{ 1 };

  uint32_t             AddRef();
  uint32_t             Release();
  uint32_t             GetRefCount();
};

struct MaterialComp
{
  Material* Material{ nullptr };

  MaterialComp() = default;
  explicit MaterialComp( Ember::Material* const material );
  MaterialComp( MaterialComp const& other ) = delete;
  MaterialComp( MaterialComp&& other ) noexcept;
  MaterialComp& operator=( MaterialComp const& other ) = delete;
  MaterialComp& operator=( MaterialComp&& other ) noexcept;
  ~MaterialComp();
};

struct GeometryComp
{
  Geometry* Geometry{ nullptr };

  GeometryComp() = default;
  explicit GeometryComp( Ember::Geometry* geometry );
  GeometryComp( GeometryComp const& other ) = delete;
  GeometryComp( GeometryComp&& other ) noexcept;
  GeometryComp& operator=( GeometryComp const& other ) = delete;
  GeometryComp& operator=( GeometryComp&& other ) noexcept;
  ~GeometryComp();
};

struct Mesh
{
  uint32_t FirstIndex;
  uint32_t IndexCount;
  uint32_t FirstVertex;
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
  flecs::query<WorldTransform const, CullInfo const, Mesh const, MaterialComp const, GeometryComp const> m_RenderQuery;

public:
  static ObjectPool<Geometry>& GeometryManager();
  static ObjectPool<Material>& MaterialManager();

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
