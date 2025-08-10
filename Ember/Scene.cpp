#include "Scene.hpp"

DirectX::XMMATRIX Ember::LocalTransform::GetTransform() const
{
  return DirectX::XMMatrixAffineTransformation( Scale, DirectX::XMVectorZero(), Rotation, Translation );
}

void Ember::LocalTransform::SetTransform( DirectX::FXMMATRIX& transform )
{
  XMMatrixDecompose( &Scale, &Rotation, &Translation, transform );
}

Ember::Primitive::~Primitive()
{
  World::LocalBoundingBoxManager().Destroy( BoundingBox );
}

Ember::Object::Object( Object* const parent )
  : m_LocalTransform{ World::LocalTransformManager().Construct() }
  , m_WorldTransform{ World::WorldTransformManager().Construct() }
  , m_LocalBoundingBox{ World::LocalBoundingBoxManager().Construct() }
  , m_WorldBoundingBox{ World::WorldBoundingBoxManager().Construct() }
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

Ember::BoundingBox& Ember::Object::GetLocalBoundingBox() const
{
  return *m_LocalBoundingBox;
}

void Ember::Object::SetLocalBoundingBox( DirectX::FXMVECTOR const& low, DirectX::FXMVECTOR const& high ) const
{
  for ( int i = 0; i < 3; i++ ) ASSERT( high.m128_f32[i] > low.m128_f32[i] );

  DirectX::BoundingBox::CreateFromPoints( m_LocalBoundingBox->AABB, low, high );
}

Ember::BoundingBox const& Ember::Object::GetWorldBoundingBox() const
{
  return *m_WorldBoundingBox;
}

void Ember::Object::SetWorldBoundingBox( DirectX::BoundingBox const& bounding_box ) const
{
  m_WorldBoundingBox->AABB = bounding_box;
}

Ember::Object* Ember::Object::GetParent() const
{
  return m_Parent;
}

void Ember::Object::SetParent( Object* parent )
{
  m_Parent = parent;
}

bool Ember::Object::IsCulled( DirectX::BoundingFrustum const& frustum ) const
{
  BoundingBox const& bb        = GetWorldBoundingBox();
  auto const         result    = frustum.Contains( bb.AABB );
  auto const         is_culled = result == DirectX::ContainmentType::DISJOINT;
  return is_culled;
}

void Ember::Object::UpdateWorldTransform( DirectX::FXMMATRIX& parent_transform )
{
  m_WorldTransform->Transform    = XMMatrixMultiply( m_LocalTransform->GetTransform(), parent_transform );
  m_WorldTransform->InvTransform = XMMatrixInverse( nullptr, m_WorldTransform->Transform );
}

DirectX::BoundingBox Ember::Object::UpdateWorldBoundingBox()
{
  m_LocalBoundingBox->AABB.Transform( m_WorldBoundingBox->AABB, GetWorldTransform().Transform );
  return m_WorldBoundingBox->AABB;
}

Ember::Object::~Object()
{
  World::LocalTransformManager().Destroy( m_LocalTransform );
  World::WorldTransformManager().Destroy( m_WorldTransform );
  World::LocalBoundingBoxManager().Destroy( m_LocalBoundingBox );
  World::WorldBoundingBoxManager().Destroy( m_WorldBoundingBox );
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

void Ember::Node::UpdateWorldTransform( DirectX::FXMMATRIX& parent_transform )
{
  Object::UpdateWorldTransform( parent_transform );
  for ( Object* child : m_Children )
  {
    child->UpdateWorldTransform( GetWorldTransform().Transform );
  }
}

DirectX::BoundingBox Ember::Node::UpdateWorldBoundingBox()
{
  auto bb = Object::UpdateWorldBoundingBox();

  for ( Object* child : m_Children )
  {
    auto cbb = child->UpdateWorldBoundingBox();
    DirectX::BoundingBox::CreateMerged( bb, cbb, bb );
  }
  SetWorldBoundingBox( bb );

  return bb;
}

void Ember::Node::Update( float const delta_seconds )
{
  for ( Object* child : m_Children )
  {
    child->Update( delta_seconds );
  }
}

void Ember::Node::Render( ID3D12GraphicsCommandList* command_list, DirectX::BoundingFrustum const& frustum ) const
{
  if ( IsCulled( frustum ) ) return;

  for ( Object* child : m_Children )
  {
    child->Render( command_list, frustum );
  }
}

void Ember::Node::RenderShadow( ID3D12GraphicsCommandList* command_list, DirectX::BoundingFrustum const& frustum ) const
{
  if ( IsCulled( frustum ) ) return;

  for ( Object* child : m_Children )
  {
    child->RenderShadow( command_list, frustum );
  }
}

Ember::Node::~Node()
{
  for ( Object* child : m_Children )
  {
    m_Children.get_allocator().delete_object( child );
  }
}

Ember::Model::Model(
    Object* parent, MeshData* mesh_data, std::span<Material*> const& materials, allocator_type const& allocator )
  : Node{ parent, allocator }, m_Materials{ materials.begin(), materials.end(), allocator }, m_MeshData{ mesh_data }
{}

Ember::Model::Model( Object* parent, MeshData* mesh_data, allocator_type const& allocator )
  : Model{ parent, mesh_data, {}, allocator }
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
  World::MeshManager().Destroy( m_MeshData );
}

Ember::Mesh::Mesh(
    Object* parent, MeshData* mesh_data, std::span<Primitive> const& primitives, allocator_type const& allocator )
  : Object{ parent }, m_MeshData{ mesh_data }, m_Primitives{ primitives.begin(), primitives.end(), allocator }
{}

void Ember::Mesh::Update( float )
{}

void Ember::Mesh::Render( ID3D12GraphicsCommandList* command_list, DirectX::BoundingFrustum const& frustum ) const
{
  if ( IsCulled( frustum ) ) return;

  auto& world_transform = GetWorldTransform();
  for ( Primitive const& primitive : m_Primitives )
  {
    DirectX::BoundingBox bb;
    primitive.BoundingBox->AABB.Transform( bb, world_transform.Transform );
    if ( frustum.Contains( bb ) == DirectX::DISJOINT ) continue;

    command_list->IASetIndexBuffer( &m_MeshData->IndexBuffer.GetIndexBufferView() );
    command_list->IASetVertexBuffers( 0, 1, &m_MeshData->VertexBuffer.GetVertexBufferView() );

    command_list->SetGraphicsRoot32BitConstants( 0, sizeof( WorldTransform ) / 4, &world_transform, 0 );
    command_list->SetGraphicsRoot32BitConstants( 1, sizeof( Material::GpuRepr ) / 4, &primitive.Material->Repr, 0 );

    command_list->DrawIndexedInstanced(
        primitive.DrawInfo.IndexCount, 1, primitive.DrawInfo.FirstIndex, primitive.DrawInfo.FirstVertex, 0 );
  }
}

void Ember::Mesh::RenderShadow( ID3D12GraphicsCommandList* command_list, DirectX::BoundingFrustum const& frustum ) const
{
  if ( IsCulled( frustum ) ) return;

  WorldTransform const& world_transform = GetWorldTransform();
  for ( Primitive const& primitive : m_Primitives )
  {
    DirectX::BoundingBox bb;
    primitive.BoundingBox->AABB.Transform( bb, world_transform.Transform );
    if ( frustum.Contains( bb ) == DirectX::DISJOINT ) continue;

    command_list->IASetIndexBuffer( &m_MeshData->IndexBuffer.GetIndexBufferView() );
    command_list->IASetVertexBuffers( 0, 1, &m_MeshData->VertexBuffer.GetVertexBufferView() );

    command_list->SetGraphicsRoot32BitConstants( 0, sizeof( DirectX::XMMATRIX ) / 4, &world_transform.Transform, 0 );

    command_list->DrawIndexedInstanced(
        primitive.DrawInfo.IndexCount, 1, primitive.DrawInfo.FirstIndex, primitive.DrawInfo.FirstVertex, 0 );
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

Ember::ObjectPool<Ember::BoundingBox>& Ember::World::LocalBoundingBoxManager()
{
  static ObjectPool<BoundingBox> local_bounding_box_manager;
  return local_bounding_box_manager;
}

Ember::ObjectPool<Ember::BoundingBox>& Ember::World::WorldBoundingBoxManager()
{
  static ObjectPool<BoundingBox> world_bounding_box_manager;
  return world_bounding_box_manager;
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
  UpdateWorldTransform( DirectX::XMMatrixIdentity() );
  UpdateWorldBoundingBox();
  Node::Update( delta_seconds );
}
