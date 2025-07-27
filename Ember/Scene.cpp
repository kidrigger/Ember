#include "Scene.hpp"

Ember::Object::Object( Object* const parent )
  : m_LocalTransform{ World::LocalTransformManager().Construct() }
  , m_WorldTransform{ World::WorldTransformManager().Construct() }
  , m_Parent{ parent }
{}

DirectX::FXMMATRIX& Ember::Object::GetLocalTransform() const
{
  return m_LocalTransform->Transform;
}

void Ember::Object::SetLocalTransform( DirectX::XMMATRIX const& transform ) const
{
  m_LocalTransform->Transform = transform;
}

Ember::WorldTransform& Ember::Object::GetWorldTransform() const
{
  return *m_WorldTransform;
}

void Ember::Object::SetWorldTransform( DirectX::XMMATRIX const& transform ) const
{
  m_LocalTransform->Transform    = XMMatrixMultiply( transform, m_Parent->GetWorldTransform().InvTransform );
  m_WorldTransform->Transform    = transform;
  m_WorldTransform->InvTransform = XMMatrixInverse( nullptr, transform );
}

Ember::Object* Ember::Object::GetParent() const
{
  return m_Parent;
}

void Ember::Object::SetParent( Object* parent )
{
  m_Parent = parent;
}

void Ember::Object::UpdateWorldTransform()
{
  m_WorldTransform->Transform =
      XMMatrixMultiply( m_LocalTransform->Transform, m_Parent->GetWorldTransform().Transform );
  m_WorldTransform->InvTransform = XMMatrixInverse( nullptr, m_WorldTransform->Transform );
}

Ember::Object::~Object()
{
  World::LocalTransformManager().Destroy( m_LocalTransform );
  World::WorldTransformManager().Destroy( m_WorldTransform );
}

std::span<Ember::Object*> Ember::Node::GetChildren()
{
  return m_Children;
}

Ember::Node::Node( Object* const parent, std::pmr::polymorphic_allocator<> const& allocator )
  : Object{ parent }, m_Children{ allocator }
{}

void Ember::Node::UpdateWorldTransform()
{
  Object::UpdateWorldTransform();
  for ( Object* child : m_Children )
  {
    child->UpdateWorldTransform();
  }
}

void Ember::Node::Update( float const delta_seconds )
{
  for ( Object* child : m_Children )
  {
    child->Update( delta_seconds );
  }
}

void Ember::Node::Render( RenderCommandQueue* render_queue )
{
  for ( Object* child : m_Children )
  {
    child->Render( render_queue );
  }
}

Ember::Model::Model(
    Object*                     parent,
    Mesh*                       mesh,
    std::span<Material*> const& materials,
    std::span<Primitive> const& primitives,
    allocator_type const&       allocator )
  : Object{ parent }
  , m_Mesh{ mesh }
  , m_Materials{ materials.begin(), materials.end(), allocator }
  , m_Primitives{ primitives.begin(), primitives.end(), allocator }
{}

void Ember::Model::Update( float )
{}

void Ember::Model::Render( RenderCommandQueue* render_queue )
{
  for ( Primitive const& primitive : m_Primitives )
  {
    render_queue->Push( GetWorldTransform(), m_Mesh, primitive.Material, primitive.Indexes );
  }
}

Ember::Model::~Model()
{
  World::MeshManager().Destroy( m_Mesh );
  for ( Material* material : m_Materials )
  {
    World::MaterialManager().Destroy( material );
  }
}

Ember::ObjectPool<Ember::LocalTransform>& Ember::World::LocalTransformManager()
{
  static ObjectPool<LocalTransform> local_transform_manager;
  return local_transform_manager;
}

Ember::ObjectPool<Ember::WorldTransform>& Ember::World::WorldTransformManager()
{
  static ObjectPool<WorldTransform> world_transform_manager;
  return world_transform_manager;
}

Ember::ObjectPool<Ember::Mesh>& Ember::World::MeshManager()
{
  static ObjectPool<Mesh> mesh_manager;
  return mesh_manager;
}

Ember::ObjectPool<Ember::Material>& Ember::World::MaterialManager()
{
  static ObjectPool<Material> material_manager;
  return material_manager;
}

Ember::World::World() : Node( nullptr, std::pmr::new_delete_resource() ), m_Allocator{ &m_PoolResource }
{}

void Ember::World::Update( float const delta_seconds )
{
  for ( Object* child : GetChildren() )
  {
    child->UpdateWorldTransform();
  }
  Node::Update( delta_seconds );
}
