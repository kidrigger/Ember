#include "Scene.hpp"

#include <algorithm>
#include <imgui.h>
#include <numeric>

#include <Graphics/RenderDevice.hpp>
#include <Util/DataUtil.hpp>
#include <Util/Profiling.hpp>
#include "Inspector.hpp"
#include "Material.hpp"

void Ember::TransformUtil::DecomposeMatrix(
    Scale* out_scale, Rotation* out_rotation, Translation* out_translation, DirectX::XMMATRIX const& matrix )
{
  ASSERT( out_scale );
  ASSERT( out_rotation );
  ASSERT( out_translation );

  DirectX::XMVECTOR scale, rotation, translation;
  ENSURE( XMMatrixDecompose( &scale, &rotation, &translation, matrix ) );

  XMStoreFloat3( &out_scale->Value, scale );
  XMStoreFloat4( &out_rotation->Value, rotation );
  XMStoreFloat3( &out_translation->Value, translation );
}

void Ember::TransformUtil::DecomposeMatrixPartial(
    Scale* out_scale, Rotation* out_rotation, Translation* out_translation, DirectX::XMMATRIX const& matrix )
{
  DirectX::XMVECTOR scale, rotation, translation;
  ENSURE( XMMatrixDecompose( &scale, &rotation, &translation, matrix ) );

  if ( out_scale ) XMStoreFloat3( &out_scale->Value, scale );
  if ( out_rotation ) XMStoreFloat4( &out_rotation->Value, rotation );
  if ( out_translation ) XMStoreFloat3( &out_translation->Value, translation );
}

DirectX::XMMATRIX Ember::TransformUtil::ConstructMatrix(
    Scale const& scale, Rotation const& rotation, Translation const& translation )
{
  return DirectX::XMMatrixAffineTransformation(
      scale.ToVector(), DirectX::XMVectorZero(), rotation.ToVector(), translation.ToVector() );
}

DirectX::XMMATRIX Ember::TransformUtil::ConstructMatrixPartial(
    Scale const* scale, Rotation const* rotation, Translation const* translation )
{
  DirectX::XMVECTOR const v_scale       = scale ? scale->ToVector() : DirectX::XMVectorSplatOne();
  DirectX::XMVECTOR const v_rotation    = rotation ? rotation->ToVector() : DirectX::XMQuaternionIdentity();
  DirectX::XMVECTOR const v_translation = translation ? translation->ToVector() : DirectX::XMVectorZero();

  return DirectX::XMMatrixAffineTransformation( v_scale, DirectX::XMVectorZero(), v_rotation, v_translation );
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

Ember::DrawList::PerBatch Ember::DrawList::Batches::Opaque() const
{
  return {
    .GeometryBuffer  = GeometryBuffer,
    .MaterialBuffer  = MaterialBuffer,
    .TopLevelAS      = TopLevelAS,
    .DrawBuffer      = DrawBuffer,
    .InstancesOffset = InstancesOffset,
    .CommandsOffset  = OpaqueCommandsOffset,
    .CommandsCount   = OpaqueCommandsCount(),
  };
}

Ember::DrawList::PerBatch Ember::DrawList::Batches::Masked() const
{
  return {
    .GeometryBuffer  = GeometryBuffer,
    .MaterialBuffer  = MaterialBuffer,
    .TopLevelAS      = TopLevelAS,
    .DrawBuffer      = DrawBuffer,
    .InstancesOffset = InstancesOffset,
    .CommandsOffset  = MaskedCommandsOffset,
    .CommandsCount   = MaskedCommandsCount(),
  };
}

Ember::DrawList::PerBatch Ember::DrawList::Batches::Transparent() const
{
  return {
    .GeometryBuffer  = GeometryBuffer,
    .MaterialBuffer  = MaterialBuffer,
    .TopLevelAS      = TopLevelAS,
    .DrawBuffer      = DrawBuffer,
    .InstancesOffset = InstancesOffset,
    .CommandsOffset  = TransparentCommandsOffset,
    .CommandsCount   = TransparentCommandsCount(),
  };
}

uint32_t Ember::DrawList::Batches::OpaqueCommandsCount() const
{
  return ( MaskedCommandsOffset - OpaqueCommandsOffset ) / ( uint32_t )sizeof( AmpCommand );
}

uint32_t Ember::DrawList::Batches::MaskedCommandsCount() const
{
  return ( TransparentCommandsOffset - MaskedCommandsOffset ) / ( uint32_t )sizeof( AmpCommand );
}

uint32_t Ember::DrawList::Batches::TransparentCommandsCount() const
{
  return ( CommandsEnd - TransparentCommandsOffset ) / ( uint32_t )sizeof( AmpCommand );
}

Ember::DrawList::DrawList(
    RenderDevice*    render_device,
    GeometryManager* geometry_manager,
    MaterialManager* material_manager,
    uint32_t const   frame_count )
  : m_RenderDevice{ render_device }
  , m_GeometryManager{ geometry_manager }
  , m_MaterialManager{ material_manager }
  , m_FrameResources{ frame_count }
{}

void Ember::DrawList::PushDraw(
    WorldTransform const& transform, Mesh const& mesh, Material const& material, BottomLevelAS const& blas )
{
  std::vector<AmpCommand>* commands;
  switch ( material->GetAlphaMode() )
  {
    case AlphaMode::kOpaque:
      commands = &m_OpaqueCommands;
      break;
    case AlphaMode::kMask:
      commands = &m_MaskedCommands;
      break;
    case AlphaMode::kBlend:
      commands = &m_TransparentCommands;
      break;
    default:
      UNREACHABLE;
  }

  uint32_t const mesh_idx = CountOf( m_Meshes );
  m_Meshes.emplace_back( mesh.VertexDataStart, mesh.VertexLiteStart, material->GetHandle(), mesh.FirstMeshlet );

  uint32_t const instance_idx = CountOf( m_Instances );
  {
    auto* instance = &m_Instances.emplace_back();
    DirectX::XMStoreFloat4x4( &instance->Transform, transform.Transform );
    DirectX::XMStoreFloat4x4( &instance->InvTransform, transform.InvTransform );
    instance->MeshID = mesh_idx;
  }

  // DXR only allows 24 bits to index into the instances.
  uint32_t constexpr static kMaxRaytracingInstances = ( 1 << 24 ) - 1;
  ASSERT( instance_idx < kMaxRaytracingInstances );

  // In any event, we quit adding instances after max (16'777'215)
  if ( instance_idx < kMaxRaytracingInstances )
  {
    auto& desc                 = m_RaytracingInstances.emplace_back();
    desc.AccelerationStructure = blas.ASBuffer.GetGPUVirtualAddress();
    desc.Flags                 = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
    auto* ptr                  = ( DirectX::XMFLOAT3X4* )&desc.Transform;
    XMStoreFloat3x4( ptr, transform.Transform );
    desc.InstanceMask                        = 0xFF;
    desc.Flags                               = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
    desc.InstanceID                          = instance_idx;
    desc.InstanceContributionToHitGroupIndex = 0xFFFFFF;
  }

  int remaining_meshlets = ( int )mesh.MeshletCount;
  int meshlet_offset     = ( int )mesh.FirstMeshlet;

  while ( remaining_meshlets > 0 )
  {
    commands->emplace_back( instance_idx, meshlet_offset, std::min( remaining_meshlets, 32 ) );

    remaining_meshlets -= 32;
    meshlet_offset     += 32;
  }
}

// Prepares the frame with only the raster draw call, skipping the Top Level Acceleration Structure
Ember::DrawList::Batches Ember::DrawList::PrepareFrame( uint32_t const frame_idx )
{
  FrameResources& resources = m_FrameResources[frame_idx];

  // Raster data
  Buffer&      unified_draw_buffer         = resources.UnifiedResourceBuffer;

  size_t const meshes_size                 = ByteSizeOf( m_Meshes );
  size_t const instances_size              = ByteSizeOf( m_Instances );
  size_t const opaque_commands_size        = ByteSizeOf( m_OpaqueCommands );
  size_t const masked_commands_size        = ByteSizeOf( m_MaskedCommands );
  size_t const transparent_commands_size   = ByteSizeOf( m_TransparentCommands );

  size_t const meshes_offset               = 0;
  size_t const instances_offset            = meshes_offset + meshes_size;
  size_t const opaque_commands_offset      = instances_offset + instances_size;
  size_t const masked_commands_offset      = opaque_commands_offset + opaque_commands_size;
  size_t const transparent_commands_offset = masked_commands_offset + masked_commands_size;
  size_t const required_size               = transparent_commands_offset + transparent_commands_size;

  if ( unified_draw_buffer.GetSize() < required_size )
  {
    unified_draw_buffer = m_RenderDevice->CreateRawStorageBuffer( required_size );
    unified_draw_buffer.SetName( L"Unified Draw Resources" );
  }

  unified_draw_buffer.Write( meshes_offset, meshes_size, DataOf( m_Meshes ) );
  unified_draw_buffer.Write( instances_offset, instances_size, DataOf( m_Instances ) );
  unified_draw_buffer.Write( opaque_commands_offset, opaque_commands_size, DataOf( m_OpaqueCommands ) );
  unified_draw_buffer.Write( masked_commands_offset, masked_commands_size, DataOf( m_MaskedCommands ) );
  unified_draw_buffer.Write( transparent_commands_offset, transparent_commands_size, DataOf( m_TransparentCommands ) );

  return {
    .GeometryBuffer            = m_GeometryManager->GetSRVHandle(),
    .MaterialBuffer            = m_MaterialManager->PrepareFrame(),
    .TopLevelAS                = {},
    .DrawBuffer                = unified_draw_buffer.GetSRVHandle(),
    .InstancesOffset           = CheckedCast<uint32_t>( instances_offset ),
    .OpaqueCommandsOffset      = CheckedCast<uint32_t>( opaque_commands_offset ),
    .MaskedCommandsOffset      = CheckedCast<uint32_t>( masked_commands_offset ),
    .TransparentCommandsOffset = CheckedCast<uint32_t>( transparent_commands_offset ),
    .CommandsEnd               = CheckedCast<uint32_t>( required_size ),
  };
}

Ember::DrawList::Batches Ember::DrawList::PrepareFrameWithRaytracing( CommandList* cmd, uint32_t const frame_idx )
{
  FrameResources& resources = m_FrameResources[frame_idx];

  // Building Raytracing Acceleration Structure
  Buffer*        desc_buf          = &resources.RaytracingInstances;
  uint32_t const rt_instances_size = U32ByteSizeOf( m_RaytracingInstances );
  if ( desc_buf->GetSize() < rt_instances_size )
  {
    *desc_buf = m_RenderDevice->CreateStorageBuffer( rt_instances_size, StrideOf( m_RaytracingInstances ) );
    wchar_t name[32];
    swprintf_s( name, 32, L"TLAS Instance Desc Buffer %d", frame_idx );
    desc_buf->SetName( name );
  }
  desc_buf->Write( 0, rt_instances_size, DataOf( m_RaytracingInstances ) );

  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS const inputs = {
    .Type          = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL,
    .Flags         = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD,
    .NumDescs      = CountOf( m_RaytracingInstances ),
    .DescsLayout   = D3D12_ELEMENTS_LAYOUT_ARRAY,
    .InstanceDescs = desc_buf->GetGPUVirtualAddress(),
  };
  // Technically, all the buffers for the BLAS should also be tracked.
  // We short-cut this by lobbing this requirement on the rest of the resource management.
  // All resources attached to entities should be held for 3 frames.
  // TODO: Make it so.
  cmd->Track( desc_buf->GetBuffer() );

  // Global Barrier prevents need to track individual buffers.
  D3D12_GLOBAL_BARRIER const barrier = {
    .SyncBefore   = D3D12_BARRIER_SYNC_BUILD_RAYTRACING_ACCELERATION_STRUCTURE,
    .SyncAfter    = D3D12_BARRIER_SYNC_BUILD_RAYTRACING_ACCELERATION_STRUCTURE,
    .AccessBefore = D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_READ,
    .AccessAfter  = D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_WRITE,
  };
  auto const barrier_group = CD3DX12_BARRIER_GROUP{ 1, &barrier };

  // TODO: Integrate into CommandList.
  cmd->Get()->Barrier( 1, &barrier_group );

  D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuild;
  m_RenderDevice->GetDevice()->GetRaytracingAccelerationStructurePrebuildInfo( &inputs, &prebuild );

  Buffer* scratch_buf = &resources.RaytracingScratch;
  if ( scratch_buf->GetSize() < prebuild.ScratchDataSizeInBytes )
  {
    *scratch_buf = m_RenderDevice->CreateRawStorageBuffer( prebuild.ScratchDataSizeInBytes );
    wchar_t name[32];
    swprintf_s( name, 32, L"TLAS Scratch Buffer %d", frame_idx );
    scratch_buf->SetName( name );
  }

  Buffer* tlas_buf = &resources.TopLevelAS;
  if ( tlas_buf->GetSize() < prebuild.ResultDataMaxSizeInBytes )
  {
    *tlas_buf = m_RenderDevice->CreateASBuffer( prebuild.ResultDataMaxSizeInBytes );
    wchar_t name[32];
    swprintf_s( name, 32, L"TLAS %d", frame_idx );
    tlas_buf->SetName( name );
  }

  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC const desc = {
    .DestAccelerationStructureData    = tlas_buf->GetGPUVirtualAddress(),
    .Inputs                           = inputs,
    .ScratchAccelerationStructureData = scratch_buf->GetGPUVirtualAddress(),
  };
  cmd->Track( scratch_buf->GetBuffer() );
  cmd->Track( tlas_buf->GetBuffer() );

  // TODO: Integrate this into CommandList.
  cmd->Get()->BuildRaytracingAccelerationStructure( &desc, 0, nullptr );

  // Raster data
  Buffer&      unified_draw_buffer         = resources.UnifiedResourceBuffer;

  size_t const meshes_size                 = ByteSizeOf( m_Meshes );
  size_t const instances_size              = ByteSizeOf( m_Instances );
  size_t const opaque_commands_size        = ByteSizeOf( m_OpaqueCommands );
  size_t const masked_commands_size        = ByteSizeOf( m_MaskedCommands );
  size_t const transparent_commands_size   = ByteSizeOf( m_TransparentCommands );

  size_t const meshes_offset               = 0;
  size_t const instances_offset            = meshes_offset + meshes_size;
  size_t const opaque_commands_offset      = instances_offset + instances_size;
  size_t const masked_commands_offset      = opaque_commands_offset + opaque_commands_size;
  size_t const transparent_commands_offset = masked_commands_offset + masked_commands_size;
  size_t const required_size               = transparent_commands_offset + transparent_commands_size;

  if ( unified_draw_buffer.GetSize() < required_size )
  {
    unified_draw_buffer = m_RenderDevice->CreateRawStorageBuffer( required_size );
    unified_draw_buffer.SetName( L"Unified Draw Resources" );
  }

  unified_draw_buffer.Write( meshes_offset, meshes_size, DataOf( m_Meshes ) );
  unified_draw_buffer.Write( instances_offset, instances_size, DataOf( m_Instances ) );
  unified_draw_buffer.Write( opaque_commands_offset, opaque_commands_size, DataOf( m_OpaqueCommands ) );
  unified_draw_buffer.Write( masked_commands_offset, masked_commands_size, DataOf( m_MaskedCommands ) );
  unified_draw_buffer.Write( transparent_commands_offset, transparent_commands_size, DataOf( m_TransparentCommands ) );

  return {
    .GeometryBuffer            = m_GeometryManager->GetSRVHandle(),
    .MaterialBuffer            = m_MaterialManager->PrepareFrame(),
    .TopLevelAS                = tlas_buf->GetSRVHandle(),
    .DrawBuffer                = unified_draw_buffer.GetSRVHandle(),
    .InstancesOffset           = CheckedCast<uint32_t>( instances_offset ),
    .OpaqueCommandsOffset      = CheckedCast<uint32_t>( opaque_commands_offset ),
    .MaskedCommandsOffset      = CheckedCast<uint32_t>( masked_commands_offset ),
    .TransparentCommandsOffset = CheckedCast<uint32_t>( transparent_commands_offset ),
    .CommandsEnd               = CheckedCast<uint32_t>( required_size ),
  };
}

void Ember::DrawList::Clear()
{
  m_RaytracingInstances.clear();
  m_Instances.clear();
  m_Meshes.clear();
  m_OpaqueCommands.clear();
  m_MaskedCommands.clear();
  m_TransparentCommands.clear();
}

size_t Ember::DrawList::GetOpaqueCount() const
{
  return m_OpaqueCommands.size();
}

size_t Ember::DrawList::GetMaskedCount() const
{
  return m_MaskedCommands.size();
}

size_t Ember::DrawList::GetTransparentCount() const
{
  return m_TransparentCommands.size();
}

size_t Ember::DrawList::GetTotalCount() const
{
  return m_OpaqueCommands.size() + m_TransparentCommands.size() + m_MaskedCommands.size();
}

Ember::World::World()
{
  m_Ecs.import <flecs::stats>();
  m_Ecs.set<flecs::Rest>( {} );

  flecs::entity world_transform = m_Ecs.component<WorldTransform>();
  flecs::entity wbb             = m_Ecs.component<WorldBoundingBox>().add( flecs::With, world_transform );
  _                             = m_Ecs.component<LocalBoundingBox>().add( flecs::With, wbb );

  _                             = m_Ecs.component<Translation>()
          .member<float>( "x", 0, offsetof( DirectX::XMFLOAT3, x ) )
          .member<float>( "y", 0, offsetof( DirectX::XMFLOAT3, y ) )
          .member<float>( "z", 0, offsetof( DirectX::XMFLOAT3, z ) )
          .add( flecs::With, world_transform )
          .set<InspectorView>( InspectorView{ []( char const* label, void* elem )
                                              {
                                                DirectX::XMFLOAT3* vec = ( DirectX::XMFLOAT3* )elem;
                                                ImGui::DragFloat3( label ? label : "Value", ( float* )vec );
                                              } } );

  _ = m_Ecs.component<Rotation>()
          .member<float>( "x", 0, offsetof( DirectX::XMFLOAT4, x ) )
          .member<float>( "y", 0, offsetof( DirectX::XMFLOAT4, y ) )
          .member<float>( "z", 0, offsetof( DirectX::XMFLOAT4, z ) )
          .member<float>( "w", 0, offsetof( DirectX::XMFLOAT4, w ) )
          .add( flecs::With, world_transform )
          .set<InspectorView>( InspectorView{ []( char const* label, void* elem )
                                              {
                                                // TODO: Avoid all this by Caching the Euler angles.

                                                DirectX::XMFLOAT4* q       = ( DirectX::XMFLOAT4* )elem;
                                                DirectX::XMFLOAT3  euler   = Quaternion::ToEuler( *q );

                                                bool               changed = false;
                                                ImGui::PushID( label ? label : "Rotation" );
                                                changed |= ImGui::SliderAngle( "Pitch", &euler.x, -89.0f, 89.0f );
                                                changed |= ImGui::SliderAngle( "Yaw", &euler.y, -180.0f, 180.0f );
                                                changed |= ImGui::SliderAngle( "Roll", &euler.z, -180.0f, 180.0f );
                                                ImGui::PopID();

                                                ImGui::Text( "%.2f %.2f %.2f %.2f", q->x, q->y, q->z, q->w );

                                                if ( changed ) *q = Quaternion::FromEuler( euler );
                                              } } );

  _ = m_Ecs.component<Scale>()
          .member<float>( "x", 0, offsetof( DirectX::XMFLOAT3, x ) )
          .member<float>( "y", 0, offsetof( DirectX::XMFLOAT3, y ) )
          .member<float>( "z", 0, offsetof( DirectX::XMFLOAT3, z ) )
          .add( flecs::With, world_transform )
          .set<InspectorView>( InspectorView{ []( char const* label, void* elem )
                                              {
                                                DirectX::XMFLOAT3* vec = ( DirectX::XMFLOAT3* )elem;
                                                ImGui::DragFloat3( label ? label : "Value", ( float* )vec );
                                              } } );

  _ = m_Ecs.component<std::string>().set<InspectorView>(
      InspectorView{ []( char const* label, void* elem )
                     {
                       std::string* str = ( std::string* )elem;
                       char         buf[256];
                       if ( ImGui::InputText( label ? label : "Value", buf, sizeof( buf ) ) )
                       {
                         *str = buf;
                       }
                     } } );

  _ = m_Ecs.component<AnimationPlayerSubcomponent>().member<flecs::entity>(
      "Player", 0, offsetof( AnimationPlayerSubcomponent, Player ) );
  _ = m_Ecs.component<AnimationPlayer>()
          .member<char>( "CurrentAnimationName", 256, offsetof( AnimationPlayer, CurrentAnimationName ) )
          .member<StringID>( "CurrentAnimationID", 0, offsetof( AnimationPlayer, CurrentAnimationID ) )
          .member<float>( "Elapsed", 0, offsetof( AnimationPlayer, Elapsed ) )
          .member<float>( "Length", 0, offsetof( AnimationPlayer, Length ) )
          .member<AnimationPlayer::State>( "CurrentState", 0, offsetof( AnimationPlayer, CurrentState ) )
          .set<InspectorView>( InspectorView{
              []( char const* label, void* elem )
              {
                AnimationPlayer* player = ( AnimationPlayer* )elem;
                char             buf[256];
                sprintf_s( buf, "%s", player->CurrentAnimationName );
                if ( ImGui::InputText(
                         label ? label : "Value", buf, sizeof( buf ), ImGuiInputTextFlags_EnterReturnsTrue ) )
                {
                  player->SetAnimation( buf );
                }

                ImGui::Text( "Elapsed: %f, Total: %f", player->Elapsed, player->Length );

                AnimationPlayer::State const previous_state = player->CurrentState;
                if ( ImGui::RadioButton( "Playing", ( int* )&previous_state, AnimationPlayer::kPlaying ) )
                {
                  player->SetState( previous_state );
                }
                ImGui::SameLine();
                if ( ImGui::RadioButton( "Paused", ( int* )&previous_state, AnimationPlayer::kPaused ) )
                {
                  player->SetState( previous_state );
                }
                if ( ImGui::RadioButton( "Stopped", ( int* )&previous_state, AnimationPlayer::kStopped ) )
                {
                  player->SetState( previous_state );
                }
              } } );

  _ = m_Ecs.component<TranslatingAnimation>().add( flecs::With, m_Ecs.component<Translation>() );
  _ = m_Ecs.component<RotatingAnimation>().add( flecs::With, m_Ecs.component<Rotation>() );
  _ = m_Ecs.component<ScalingAnimation>().add( flecs::With, m_Ecs.component<Scale>() );

  m_Ecs.observer<AnimationPlayer>()
      .event( flecs::OnAdd )
      .each(
          []( flecs::entity const& entity, AnimationPlayer& player )
          {
            std::queue<flecs::entity> bfs_subtree;
            bfs_subtree.push( entity );
            while ( not bfs_subtree.empty() )
            {
              flecs::entity ent = bfs_subtree.front();
              bfs_subtree.pop();

              if ( ent.has<TranslatingAnimation>() or ent.has<RotatingAnimation>() or ent.has<ScalingAnimation>() )
              {
                ent.set<AnimationPlayerSubcomponent>( { entity } );
              }

              ent.children( [&]( flecs::entity const child ) { bfs_subtree.push( child ); } );
            }
          } );

  m_Ecs.observer<AnimationPlayer>()
      .event( flecs::OnRemove )
      .each(
          []( flecs::entity const& entity, AnimationPlayer& player )
          {
            std::queue<flecs::entity> bfs_subtree;
            bfs_subtree.push( entity );
            while ( not bfs_subtree.empty() )
            {
              flecs::entity ent = bfs_subtree.front();
              bfs_subtree.pop();

              if ( auto* it = ent.try_get<AnimationPlayerSubcomponent>(); it and it->Player == entity )
              {
                _ = ent.remove<AnimationPlayerSubcomponent>();
              }

              ent.children( [&]( flecs::entity const child ) { bfs_subtree.push( child ); } );
            }
          } );

  m_UpdateRootWorldTransformSys =
      m_Ecs.system<WorldTransform, Translation const*, Rotation const*, Scale const*>()
          .without( flecs::ChildOf )
          .each(
              []( WorldTransform& wt, Translation const* translation, Rotation const* rotation, Scale const* scale )
              {
                wt.Transform    = TransformUtil::ConstructMatrixPartial( scale, rotation, translation );
                wt.InvTransform = XMMatrixInverse( nullptr, wt.Transform );
              } );

  m_UpdateWorldTransformSys =
      m_Ecs.system<WorldTransform, Translation const*, Rotation const*, Scale const*, WorldTransform const>()
          .term_at( 4 )
          .parent()
          .cascade()
          .each(
              []( WorldTransform& wt,
                  Translation const* translation,
                  Rotation const* rotation,
                  Scale const* scale,
                  WorldTransform const& parent_wt )
              {
                wt.Transform = XMMatrixMultiply(
                    TransformUtil::ConstructMatrixPartial( scale, rotation, translation ), parent_wt.Transform );

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

  m_UpdateAnimationPlayer = m_Ecs.system<AnimationPlayer>().each(
      []( flecs::iter& it, size_t, AnimationPlayer& player )
      {
        if ( player.CurrentState == AnimationPlayer::State::kPlaying )
        {
          player.Elapsed += it.delta_time();
        }
      } );

  m_UpdateAnimationTranslation =
      m_Ecs.system<AnimationPlayerSubcomponent const, TranslatingAnimation const, Translation>().each(
          []( flecs::entity e,
              AnimationPlayerSubcomponent const& player,
              TranslatingAnimation const& anim,
              Translation& translation )
          {
            AnimationPlayer const* anim_player = player.Player.try_get_mut<AnimationPlayer>();
            if ( not anim_player )
            {
              _ = e.remove<AnimationPlayerSubcomponent>();
              return;
            }

            if ( auto const it = anim.Animations.Find( anim_player->CurrentAnimationID ); it != anim.Animations.end() )
            {
              float const          elapsed = std::fmodf( anim_player->Elapsed, it->second.Length );

              std::optional<float> timeline_lo;
              std::optional<float> timeline_hi;
              DirectX::XMFLOAT3    value_lo = { 0.0f, 0.0f, 0.0f };
              DirectX::XMFLOAT3    value_hi = { 0.0f, 0.0f, 0.0f };
              for ( auto const& [time, value] : it->second.Keyframes )
              {
                if ( elapsed < time )
                {
                  value_hi    = value;
                  timeline_hi = time;
                  break;
                }
                value_lo    = value;
                timeline_lo = time;
              }

              if ( not timeline_lo.has_value() )
              {
                translation.Value = value_hi;
                return;
              }
              if ( not timeline_hi.has_value() )
              {
                translation.Value = value_lo;
                return;
              }

              float const factor = ( elapsed - timeline_lo.value() ) / ( timeline_hi.value() - timeline_lo.value() );

              translation.Value  = {
                std::lerp( value_lo.x, value_hi.x, factor ),
                std::lerp( value_lo.y, value_hi.y, factor ),
                std::lerp( value_lo.z, value_hi.z, factor ),
              };
            }
          } );

  m_UpdateAnimationRotation = m_Ecs.system<AnimationPlayerSubcomponent const, RotatingAnimation const, Rotation>().each(
      []( flecs::entity e,
          AnimationPlayerSubcomponent const& player,
          RotatingAnimation const& anim,
          Rotation& rotation )
      {
        AnimationPlayer const* anim_player = player.Player.try_get_mut<AnimationPlayer>();
        if ( not anim_player )
        {
          _ = e.remove<AnimationPlayerSubcomponent>();
          return;
        }

        if ( auto const it = anim.Animations.Find( anim_player->CurrentAnimationID ); it != anim.Animations.end() )
        {
          float const          elapsed = std::fmodf( anim_player->Elapsed, it->second.Length );

          std::optional<float> timeline_lo;
          std::optional<float> timeline_hi;
          DirectX::XMFLOAT4    value_lo = { 0.0f, 0.0f, 0.0f, 1.0f };
          DirectX::XMFLOAT4    value_hi = { 0.0f, 0.0f, 0.0f, 1.0f };
          for ( auto const& [time, value] : it->second.Keyframes )
          {
            if ( elapsed < time )
            {
              value_hi    = value;
              timeline_hi = time;
              break;
            }
            value_lo    = value;
            timeline_lo = time;
          }

          if ( not timeline_lo.has_value() )
          {
            XMStoreFloat4( &rotation.Value, DirectX::XMQuaternionNormalize( XMLoadFloat4( &value_hi ) ) );
            return;
          }
          if ( not timeline_hi.has_value() )
          {
            XMStoreFloat4( &rotation.Value, DirectX::XMQuaternionNormalize( XMLoadFloat4( &value_lo ) ) );
            return;
          }

          float const factor = ( elapsed - timeline_lo.value() ) / ( timeline_hi.value() - timeline_lo.value() );

          XMStoreFloat4(
              &rotation.Value,
              DirectX::XMQuaternionNormalize(
                  DirectX::XMVectorLerp( XMLoadFloat4( &value_lo ), XMLoadFloat4( &value_hi ), factor ) ) );
        }
      } );

  m_UpdateAnimationScale = m_Ecs.system<AnimationPlayerSubcomponent const, ScalingAnimation const, Scale>().each(
      []( flecs::entity e, AnimationPlayerSubcomponent const& player, ScalingAnimation const& anim, Scale& scale )
      {
        AnimationPlayer const* anim_player = player.Player.try_get_mut<AnimationPlayer>();
        if ( not anim_player )
        {
          _ = e.remove<AnimationPlayerSubcomponent>();
          return;
        }

        if ( auto const it = anim.Animations.Find( anim_player->CurrentAnimationID ); it != anim.Animations.end() )
        {
          float const          elapsed = std::fmodf( anim_player->Elapsed, it->second.Length );

          std::optional<float> timeline_lo;
          std::optional<float> timeline_hi;
          DirectX::XMFLOAT3    value_lo = { 1.0f, 1.0f, 1.0f };
          DirectX::XMFLOAT3    value_hi = { 1.0f, 1.0f, 1.0f };
          for ( auto const& [time, value] : it->second.Keyframes )
          {
            if ( elapsed < time )
            {
              value_hi    = value;
              timeline_hi = time;
              break;
            }
            value_lo    = value;
            timeline_lo = time;
          }

          if ( not timeline_lo.has_value() )
          {
            scale.Value = value_hi;
            return;
          }
          if ( not timeline_hi.has_value() )
          {
            scale.Value = value_lo;
            return;
          }

          float const factor = ( elapsed - timeline_lo.value() ) / ( timeline_hi.value() - timeline_lo.value() );

          scale.Value        = {
            std::lerp( value_lo.x, value_hi.x, factor ),
            std::lerp( value_lo.y, value_hi.y, factor ),
            std::lerp( value_lo.z, value_hi.z, factor ),
          };
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

  {
    ZoneScopedN( "Animation" );
    m_UpdateAnimationPlayer.run( delta_time );
    m_UpdateAnimationTranslation.run( delta_time );
    m_UpdateAnimationRotation.run( delta_time );
    m_UpdateAnimationScale.run( delta_time );
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
