#include "DrawList.hpp"

#include <Util/DataUtil.hpp>
#include "Material.hpp"
#include "MaterialManager.hpp"
#include "Scene.hpp"

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
  m_Meshes.emplace_back(
      mesh.VertexDataStart, mesh.VertexLiteStart, material->GetHandle(), mesh.FirstMeshlet, mesh.FirstIndex );

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
