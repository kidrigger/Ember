#include "Material.hpp"

#include "MaterialManager.hpp"

Ember::Material::ReprInfo::ReprInfo( MaterialManager* const manager, MaterialHandle handle )
  : Manager{ manager }, Handle{ std::move( handle ) }
{
  ASSERT( manager );
}

Ember::Material::ReprInfo::ReprInfo( ReprInfo&& other ) noexcept
  : Manager{ other.Manager }, Handle{ std::move( other.Handle ) }
{
  other.Manager = nullptr;
}

Ember::Material::ReprInfo& Ember::Material::ReprInfo::operator=( ReprInfo&& other ) noexcept
{
  if ( this == &other ) return *this;
  if ( Manager ) Manager->Free( Handle );

  Manager       = other.Manager;
  Handle        = std::move( other.Handle );

  other.Manager = nullptr;
  return *this;
}

Ember::Material::ReprInfo::~ReprInfo()
{
  if ( not Manager ) return;

  Manager->Free( Handle );
}

uint32_t Ember::Material::AddRef()
{
  return ++m_RefCount;
}

uint32_t Ember::Material::Release()
{
  return --m_RefCount;
}

uint32_t Ember::Material::GetRefCount()
{
  return m_RefCount;
}

Ember::MaterialHandle Ember::Material::GetHandle() const
{
  return m_Repr.Handle;
}

Ember::Material::Material(
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
