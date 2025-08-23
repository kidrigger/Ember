#pragma once

#include "Color.hpp"
#include "DeviceHandle.hpp"
#include "Texture.hpp"

namespace Ember
{
class MaterialManager;

class Material
{
public:
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

private:
  struct ReprInfo
  {
    MaterialManager* Manager;
    MaterialHandle   Handle;

    ReprInfo( MaterialManager* manager, MaterialHandle handle );
    ReprInfo( ReprInfo const& other ) = delete;
    ReprInfo( ReprInfo&& other ) noexcept;
    ReprInfo& operator=( ReprInfo const& other ) = delete;
    ReprInfo& operator=( ReprInfo&& other ) noexcept;
    ~ReprInfo();
  };

  Texture              m_BaseColorTexture;
  Texture              m_NormalTexture;
  Texture              m_MetalRoughTexture;
  Texture              m_EmissiveTexture;
  ReprInfo             m_Repr;

  std::atomic_uint32_t m_RefCount{ 1 };

public:
  uint32_t       AddRef();
  uint32_t       Release();
  uint32_t       GetRefCount();

  MaterialHandle GetHandle() const;

  Material(
      Texture          base_color_texture,
      Texture          normal_texture,
      Texture          metal_rough_texture,
      Texture          emissive_texture,
      MaterialManager* material_manager,
      MaterialHandle   handle );
};

} // namespace Ember
