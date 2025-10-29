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

struct Static
{};

struct LocalTransform
{
  DirectX::XMFLOAT3               Translation{ 0.0f, 0.0f, 0.0f };
  DirectX::XMFLOAT4               Rotation{ 0.0f, 0.0f, 0.0f, 1.0f };
  DirectX::XMFLOAT3               Scale{ 1.0f, 1.0f, 1.0f };

  [[nodiscard]] DirectX::XMMATRIX GetTransform() const;
  void                            SetTransform( DirectX::FXMMATRIX const& transform );
};

struct WorldTransform
{
  DirectX::XMMATRIX Transform{ DirectX::XMMatrixIdentity() };
  DirectX::XMMATRIX InvTransform{ DirectX::XMMatrixIdentity() };

  DirectX::XMFLOAT3 GetTranslation() const;
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

struct GeometryImpl
{
  GeometryAllocation   GeometryAlloc;
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
  flecs::world  m_Ecs;
  flecs::system m_UpdateRootWorldTransformSys;
  flecs::system m_UpdateWorldTransformSys;
  flecs::system m_PrimeActualWorldAABBSys;
  flecs::system m_PrimeCollectingWorldAABBSys;
  flecs::system m_UpdateWorldAABBSys;

public:
  static ObjectPool<GeometryImpl>& GeometryManager();
  static ObjectPool<MaterialImpl>& MaterialManager();

  World();

  void Update( float delta_seconds ) const;

  //
  flecs::world const& GetECS() const;
  flecs::world&       GetECS();
};

} // namespace Ember
