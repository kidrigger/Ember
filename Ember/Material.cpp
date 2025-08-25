#include "Material.hpp"

#include "MaterialManager.hpp"

Ember::MaterialImpl::ReprInfo::ReprInfo( MaterialManager* const manager, MaterialHandle handle )
  : Manager{ manager }, Handle{ std::move( handle ) }
{
  ASSERT( manager );
}

Ember::MaterialImpl::ReprInfo::ReprInfo( ReprInfo&& other ) noexcept
  : Manager{ other.Manager }, Handle{ std::move( other.Handle ) }
{
  other.Manager = nullptr;
}

Ember::MaterialImpl::ReprInfo& Ember::MaterialImpl::ReprInfo::operator=( ReprInfo&& other ) noexcept
{
  if ( this == &other ) return *this;
  if ( Manager ) Manager->Free( Handle );

  Manager       = other.Manager;
  Handle        = std::move( other.Handle );

  other.Manager = nullptr;
  return *this;
}

Ember::MaterialImpl::ReprInfo::~ReprInfo()
{
  if ( not Manager ) return;

  Manager->Free( Handle );
}

uint32_t Ember::MaterialImpl::AddRef()
{
  return ++m_RefCount;
}

uint32_t Ember::MaterialImpl::Release()
{
  return --m_RefCount;
}

uint32_t Ember::MaterialImpl::GetRefCount()
{
  return m_RefCount;
}

Ember::MaterialHandle Ember::MaterialImpl::GetHandle() const
{
  return m_Repr.Handle;
}

Ember::MaterialImpl::MaterialImpl(
    Texture                base_color_texture,
    Texture                normal_texture,
    Texture                metal_rough_texture,
    Texture                emissive_texture,
    MaterialManager* const material_manager,
    MaterialHandle         handle )
  : m_BaseColorTexture{ std::move( base_color_texture ) }
  , m_NormalTexture{ std::move( normal_texture ) }
  , m_MetalRoughTexture{ std::move( metal_rough_texture ) }
  , m_EmissiveTexture{ std::move( emissive_texture ) }
  , m_Repr{ material_manager, handle }
{}
