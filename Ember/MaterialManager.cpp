#include "MaterialManager.hpp"

#include "RenderDevice.hpp"

Ember::MaterialManager::MaterialManager( Buffer data_buffer, uint32_t const max_materials )
  : m_DataBuffer{ std::move( data_buffer ) }, m_FreeList{ max_materials }
{}

void Ember::MaterialManager::Create(
    MaterialManager* material_manager, RenderDevice* render_device, uint32_t const max_materials )
{
  size_t const req_size = max_materials * sizeof( Material::GpuRepr );
  ASSERT( req_size <= UINT32_MAX );
  auto buffer = render_device->CreateStorageBuffer( ( uint32_t )req_size, sizeof( Material::GpuRepr ) );

  new ( material_manager ) MaterialManager{ std::move( buffer ), max_materials };
}

Ember::MaterialHandle Ember::MaterialManager::CreateMaterialHandle( Material::GpuRepr const& material )
{
  std::lock_guard lock_guard{ m_Lock };

  uint32_t const  index      = m_FreeList.Allocate();
  uint32_t const  page_idx   = ( index & kPageIndexMask ) >> kPageIndexOffset;

  size_t const    page_count = m_Pages.size();
  if ( page_idx >= page_count )
  {
    m_Pages.emplace_back();
    m_PageDirty.emplace_back( true );
  }

  uint32_t const     element_idx = index & kElementIndexMask;
  Material::GpuRepr* location    = &m_Pages[page_idx][element_idx];

  memcpy( location, &material, sizeof material );

  m_PageDirty[page_idx] = true;

  return MaterialHandle{ index };
}

void Ember::MaterialManager::Free( MaterialHandle const handle )
{
  if ( handle.IsNull() ) return;
  m_FreeList.Free( handle.GetInner() );
}

void Ember::MaterialManager::UpdateReprs()
{
  uint32_t i = 0;
  for ( bool& dirty : m_PageDirty )
  {
    if ( dirty )
    {
      m_DataBuffer.Write(
          i * kElementsPerPage * sizeof( Material::GpuRepr ),
          kElementsPerPage * sizeof( Material::GpuRepr ),
          &m_Pages[i] );
      dirty = false;
    }
    i++;
  }
}

Ember::SRVHandle Ember::MaterialManager::PrepareFrame()
{
  UpdateReprs();
  return m_DataBuffer.GetSRVHandle();
}

Ember::MaterialManager::~MaterialManager()
{
  ASSERT( m_FreeList.InUse() == 0 );
}
