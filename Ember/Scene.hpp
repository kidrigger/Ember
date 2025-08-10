#pragma once

#include <memory_resource>
#include <span>

#include "Buffer.hpp"
#include "Color.hpp"
#include "ObjectPool.hpp"
#include "Texture.hpp"
#include "Util/DirectXHeaders.hpp"

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

struct BoundingBox
{
  DirectX::BoundingBox AABB;
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

  Texture BaseColorTexture;
  Texture NormalTexture;
  Texture MetalRoughTexture;
  Texture EmissiveTexture;
  GpuRepr Repr;
};

struct MeshData
{
  Buffer VertexBuffer;
  Buffer IndexBuffer;
};

struct Primitive
{
  struct Data
  {
    uint32_t FirstIndex;
    uint32_t IndexCount;
    uint32_t FirstVertex;
  };
  Material*    Material{ nullptr };
  BoundingBox* BoundingBox{ nullptr };
  Data         DrawInfo;

  Primitive() = default;
  Primitive( Ember::Material* const material, Ember::BoundingBox* const bounding_box, Data draw_info )
    : Material{ material }, BoundingBox{ bounding_box }, DrawInfo{ std::move( draw_info ) }
  {}
  Primitive( Primitive const& other ) = default;
  Primitive( Primitive&& other ) noexcept
    : Material{ other.Material }, BoundingBox{ other.BoundingBox }, DrawInfo{ std::move( other.DrawInfo ) }
  {
    other.Material    = nullptr;
    other.BoundingBox = nullptr;
  }
  Primitive& operator=( Primitive const& other ) = default;
  Primitive& operator=( Primitive&& other ) noexcept
  {
    if ( this == &other ) return *this;
    std::swap( Material, other.Material );
    std::swap( BoundingBox, other.BoundingBox );
    std::swap( DrawInfo, other.DrawInfo );
    return *this;
  }
  ~Primitive();
};

struct RenderCommandQueue
{
  std::vector<WorldTransform>  Transforms;
  std::vector<MeshData*>       Meshes;
  std::vector<Material*>       Materials;
  std::vector<Primitive::Data> Primitives;
  std::vector<uint64_t>        Cull;

  [[nodiscard]] size_t         Count() const
  {
    return Transforms.size();
  }

  void Clear()
  {
    Transforms.clear();
    Meshes.clear();
    Materials.clear();
    Primitives.clear();
    Cull.clear();
  }

  void Push(
      WorldTransform const&  transform,
      MeshData*              mesh,
      Material*              material,
      Primitive::Data const& primitive_data,
      uint64_t const         cull_layers )
  {
    Transforms.push_back( transform );
    Meshes.push_back( mesh );
    Materials.push_back( material );
    Primitives.push_back( primitive_data );
    Cull.push_back( cull_layers );
  }
};

class Object
{
  LocalTransform* m_LocalTransform;
  WorldTransform* m_WorldTransform;
  BoundingBox*    m_LocalBoundingBox;
  BoundingBox*    m_WorldBoundingBox;

  Object*         m_Parent;

public:
  explicit Object( Object* parent );

  [[nodiscard]] DirectX::FXMMATRIX GetLocalTransform() const;
  void                             SetLocalTransform( DirectX::XMMATRIX const& transform ) const;
  void                             SetLocalTransform(
                                  DirectX::XMVECTOR const& translation, DirectX::XMVECTOR const& rotation, DirectX::XMVECTOR const& scale ) const;
  [[nodiscard]] DirectX::XMVECTOR  GetLocalTranslation() const;
  void                             SetLocalTranslation( DirectX::XMVECTOR const& translation ) const;
  [[nodiscard]] DirectX::XMVECTOR  GetLocalRotation() const;
  void                             SetLocalRotation( DirectX::XMVECTOR const& rotation ) const;
  [[nodiscard]] DirectX::XMVECTOR  GetLocalScale() const;
  void                             SetLocalScale( DirectX::XMVECTOR const& scale ) const;

  [[nodiscard]] WorldTransform&    GetWorldTransform() const;
  void                             SetWorldTransform( DirectX::XMMATRIX const& transform ) const;

  [[nodiscard]] BoundingBox&       GetLocalBoundingBox() const;
  void                             SetLocalBoundingBox( DirectX::FXMVECTOR& low, DirectX::FXMVECTOR& high ) const;

  [[nodiscard]] BoundingBox const& GetWorldBoundingBox() const;
  void                             SetWorldBoundingBox( DirectX::BoundingBox const& bounding_box ) const;

  [[nodiscard]] Object*            GetParent() const;
  void                             SetParent( Object* parent );

  bool                             IsCulled( DirectX::BoundingFrustum const& frustum ) const;
  virtual void                     Update( float delta_seconds )                                                = 0;
  virtual void Render( ID3D12GraphicsCommandList* command_list, DirectX::BoundingFrustum const& frustum ) const = 0;
  virtual void RenderShadow(
      ID3D12GraphicsCommandList* command_list, DirectX::BoundingFrustum const& frustum ) const = 0;

  virtual void                 UpdateWorldTransform( DirectX::FXMMATRIX& parent_transform );
  virtual DirectX::BoundingBox UpdateWorldBoundingBox();

  Object( Object const& other )                = delete;
  Object( Object&& other ) noexcept            = delete;
  Object& operator=( Object const& other )     = delete;
  Object& operator=( Object&& other ) noexcept = delete;
  virtual ~Object();
};

class Node : public Object
{
  std::pmr::vector<Object*> m_Children;

protected:
  std::span<Object*> GetChildren();

public:
  using allocator_type = std::pmr::polymorphic_allocator<>;

  explicit Node( Object* parent, allocator_type const& allocator = {} );

  template <std::derived_from<Object> T>
  T* CreateChildObject( auto&&... args )
    requires std::constructible_from<T, Object*, decltype( args )...>
  {
    T* object = m_Children.get_allocator().allocate_object<T>();
    m_Children.get_allocator().construct( object, this, std::forward<decltype( args )>( args )... );
    m_Children.push_back( object );
    return object;
  }

  void                 AddChild( Object* object );

  void                 UpdateWorldTransform( DirectX::FXMMATRIX& parent_transform ) override;
  DirectX::BoundingBox UpdateWorldBoundingBox() override;

  void                 Update( float delta_seconds ) override;
  void Render( ID3D12GraphicsCommandList* command_list, DirectX::BoundingFrustum const& frustum ) const override;
  void RenderShadow( ID3D12GraphicsCommandList* command_list, DirectX::BoundingFrustum const& frustum ) const override;

  Node( Node const& other )                = delete;
  Node( Node&& other ) noexcept            = delete;
  Node& operator=( Node const& other )     = delete;
  Node& operator=( Node&& other ) noexcept = delete;
  ~Node() override;
};

class Model final : public Node
{
  std::pmr::vector<Material*> m_Materials;
  MeshData*                   m_MeshData;

public:
  using allocator_type = std::pmr::polymorphic_allocator<>;

  Model(
      Object*                     parent,
      MeshData*                   mesh_data,
      std::span<Material*> const& materials,
      allocator_type const&       allocator = {} );
  Model( Object* parent, MeshData* mesh_data, allocator_type const& allocator = {} );

  void AddMaterial( Material* material );

  Model( Model const& other )                = delete;
  Model( Model&& other ) noexcept            = delete;
  Model& operator=( Model const& other )     = delete;
  Model& operator=( Model&& other ) noexcept = delete;
  ~Model() override;
};

class Mesh final : public Object
{
  MeshData*                   m_MeshData;
  std::pmr::vector<Primitive> m_Primitives;

public:
  using allocator_type = std::pmr::polymorphic_allocator<>;

  Mesh(
      Object*                     parent,
      MeshData*                   mesh_data,
      std::span<Primitive> const& primitives,
      allocator_type const&       allocator = {} );

  void Update( float delta_seconds ) override;
  void Render( ID3D12GraphicsCommandList* command_list, DirectX::BoundingFrustum const& frustum ) const override;
  void RenderShadow( ID3D12GraphicsCommandList* command_list, DirectX::BoundingFrustum const& frustum ) const override;
};

class World final : public Node
{
  std::pmr::unsynchronized_pool_resource m_PoolResource;
  std::pmr::polymorphic_allocator<>      m_Allocator;

public:
  static ObjectPool<LocalTransform>& LocalTransformManager();
  static ObjectPool<WorldTransform>& WorldTransformManager();
  static ObjectPool<BoundingBox>&    LocalBoundingBoxManager();
  static ObjectPool<BoundingBox>&    WorldBoundingBoxManager();
  static ObjectPool<MeshData>&       MeshManager();
  static ObjectPool<Material>&       MaterialManager();

  World();

  void Update( float delta_seconds ) override;

  template <std::derived_from<Object> T>
  T* CreateObject( auto&&... args )
    requires std::constructible_from<T, Object*, decltype( args )...>
  {
    return CreateChildObject<T>( std::forward<decltype( args )>( args )... );
  }
};

} // namespace Ember
