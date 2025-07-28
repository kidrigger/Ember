#include "Scene.hpp"

DirectX::XMMATRIX Ember::LocalTransform::GetTransform() const
{
  return DirectX::XMMatrixAffineTransformation( Scale, DirectX::XMVectorZero(), Rotation, Translation );
}

void Ember::LocalTransform::SetTransform( DirectX::FXMMATRIX& transform )
{
  XMMatrixDecompose( &Scale, &Rotation, &Translation, transform );
}

Ember::Object::Object( Object* const parent )
  : m_LocalTransform{ World::LocalTransformManager().Construct() }
  , m_WorldTransform{ World::WorldTransformManager().Construct() }
  , m_Parent{ parent }
{}

DirectX::FXMMATRIX Ember::Object::GetLocalTransform() const
{
  return m_LocalTransform->GetTransform();
}

void Ember::Object::SetLocalTransform( DirectX::XMMATRIX const& transform ) const
{
  m_LocalTransform->SetTransform( transform );
}

void Ember::Object::SetLocalTransform(
    DirectX::XMVECTOR const& translation, DirectX::XMVECTOR const& rotation, DirectX::XMVECTOR const& scale ) const
{
  m_LocalTransform->Translation = translation;
  m_LocalTransform->Rotation    = rotation;
  m_LocalTransform->Scale       = scale;
}

DirectX::XMVECTOR Ember::Object::GetLocalTranslation() const
{
  return m_LocalTransform->Translation;
}

void Ember::Object::SetLocalTranslation( DirectX::XMVECTOR const& translation ) const
{
  m_LocalTransform->Translation = translation;
}

DirectX::XMVECTOR Ember::Object::GetLocalRotation() const
{
  return m_LocalTransform->Rotation;
}

void Ember::Object::SetLocalRotation( DirectX::XMVECTOR const& rotation ) const
{
  m_LocalTransform->Rotation = rotation;
}

DirectX::XMVECTOR Ember::Object::GetLocalScale() const
{
  return m_LocalTransform->Scale;
}

void Ember::Object::SetLocalScale( DirectX::XMVECTOR const& scale ) const
{
  m_LocalTransform->Scale = scale;
}

Ember::WorldTransform& Ember::Object::GetWorldTransform() const
{
  return *m_WorldTransform;
}

void Ember::Object::SetWorldTransform( DirectX::XMMATRIX const& transform ) const
{
  SetLocalTransform( XMMatrixMultiply( transform, m_Parent->GetWorldTransform().InvTransform ) );
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
      XMMatrixMultiply( m_LocalTransform->GetTransform(), m_Parent->GetWorldTransform().Transform );
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

void Ember::Node::AddChild( Object* object )
{
  // If something has a child, it must be a Node.
  Node* parent = ( Node* )object->GetParent();
  std::erase( parent->m_Children, object );
  object->SetParent( this );
  m_Children.push_back( object );
}

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

Ember::Node::~Node()
{
  for ( Object* child : m_Children )
  {
    m_Children.get_allocator().delete_object( child );
  }
}


Ember::Model::Model( Object* parent, std::span<Material*> const& materials, allocator_type const& allocator )
  : Node{ parent, allocator }, m_Materials{ materials.begin(), materials.end(), allocator }
{}

Ember::Model::Model( Object* parent, allocator_type const& allocator )
  : Node{ parent, allocator }, m_Materials{ allocator }
{}

void Ember::Model::AddMaterial( Material* material )
{
  m_Materials.push_back( material );
}

Ember::Model::~Model()
{
  for ( Material* material : m_Materials )
  {
    World::MaterialManager().Destroy( material );
  }
}

Ember::Mesh::Mesh(
    Object* parent, MeshData* mesh_data, std::span<Primitive> const& primitives, allocator_type const& allocator )
  : Object{ parent }, m_MeshData{ mesh_data }, m_Primitives{ primitives.begin(), primitives.end(), allocator }
{}

void Ember::Mesh::Update( float )
{}

void Ember::Mesh::Render( RenderCommandQueue* render_queue )
{
  for ( Primitive const& primitive : m_Primitives )
  {
    render_queue->Push( GetWorldTransform(), m_MeshData, primitive.Material, primitive.DrawInfo );
  }
}

Ember::Mesh::~Mesh()
{
  World::MeshManager().Destroy( m_MeshData );
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

Ember::ObjectPool<Ember::MeshData>& Ember::World::MeshManager()
{
  static ObjectPool<MeshData> mesh_manager;
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
