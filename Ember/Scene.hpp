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

struct Quaternion
{
  DirectX::XMFLOAT4        Inner;

  static DirectX::XMFLOAT3 ToEuler( DirectX::XMFLOAT4 quat )
  {
    quat.x                      = -quat.x;
    DirectX::XMMATRIX const mat = DirectX::XMMatrixRotationQuaternion( XMLoadFloat4( &quat ) );
    DirectX::XMFLOAT3       euler;
    euler.x = -std::asin( mat.r[1].m128_f32[2] );                        // Pitch
    euler.y = std::atan2( -mat.r[0].m128_f32[2], mat.r[2].m128_f32[2] ); // Yaw
    euler.z = std::atan2( -mat.r[1].m128_f32[0], mat.r[1].m128_f32[1] ); // Roll
    return euler;
  }

  static DirectX::XMFLOAT4 FromEuler( DirectX::XMFLOAT3 euler )
  {
    DirectX::XMFLOAT4 quat;
    XMStoreFloat4( &quat, DirectX::XMQuaternionRotationRollPitchYaw( euler.x, euler.y, euler.z ) );
    return quat;
  }
};

struct Translation
{
  DirectX::XMFLOAT3 Value{ 0.0f, 0.0f, 0.0f };

  Translation() = default;
  Translation( float const x, float const y, float const z ) : Value{ x, y, z }
  {}
  explicit Translation( DirectX::XMFLOAT3 const& vec ) : Value{ vec }
  {}
  explicit Translation( DirectX::FXMVECTOR vec )
  {
    XMStoreFloat3( &Value, vec );
  }

  DirectX::XMVECTOR ToVector() const
  {
    return XMLoadFloat3( &Value );
  }
};

struct Rotation
{
  DirectX::XMFLOAT4 Value{ 0.0f, 0.0f, 0.0f, 1.0f };

  Rotation() = default;
  Rotation( float const x, float const y, float const z, float const w ) : Value{ x, y, z, w }
  {}
  explicit Rotation( DirectX::XMFLOAT4 const& quat ) : Value{ quat }
  {}
  explicit Rotation( DirectX::FXMVECTOR quat )
  {
    XMStoreFloat4( &Value, quat );
  }

  DirectX::XMVECTOR ToVector() const
  {
    return XMLoadFloat4( &Value );
  }

  explicit operator DirectX::XMVECTOR() const
  {
    return ToVector();
  }
};

struct RotationEuler
{
  float Pitch{ 0.0f };
  float Yaw{ 0.0f };
  float Roll{ 0.0f };

  RotationEuler() = default;
  RotationEuler( float const pitch, float const yaw, float const roll ) : Pitch{ pitch }, Yaw{ yaw }, Roll{ roll }
  {}

  RotationEuler( DirectX::XMFLOAT3 const& euler ) : Pitch{ euler.x }, Yaw{ euler.y }, Roll{ euler.z }
  {}
};

struct Scale
{
  DirectX::XMFLOAT3 Value{ 1.0f, 1.0f, 1.0f };

  Scale() = default;
  Scale( float const x, float const y, float const z ) : Value{ x, y, z }
  {}
  explicit Scale( DirectX::XMFLOAT3 const& vec ) : Value{ vec }
  {}
  explicit Scale( DirectX::FXMVECTOR vec )
  {
    XMStoreFloat3( &Value, vec );
  }

  DirectX::XMVECTOR ToVector() const
  {
    return XMLoadFloat3( &Value );
  }
};

namespace TransformUtil
{
// Decomposes a matrix into scale, rotation, and translation components.
// All output parameters must be non-null.
void DecomposeMatrix(
    Scale* out_scale, Rotation* out_rotation, Translation* out_translation, DirectX::XMMATRIX const& matrix );

// Same as DecomposeMatrix but allows optional outputs.
void DecomposeMatrixPartial(
    Scale* out_scale, Rotation* out_rotation, Translation* out_translation, DirectX::XMMATRIX const& matrix );

// Constructs a transformation matrix from scale, rotation, and translation components.
DirectX::XMMATRIX ConstructMatrix( Scale const& scale, Rotation const& rotation, Translation const& translation );

// Constructs a transformation matrix from scale, rotation, and translation components.
// Uses default values for any missing components.
//
// Default values:
// Scale = 1
// Rotation = identity
// Translation = origin
DirectX::XMMATRIX ConstructMatrixPartial(
    Scale const* scale, Rotation const* rotation, Translation const* translation );

} // namespace TransformUtil

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
