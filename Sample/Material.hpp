#pragma once

#include <Graphics/DeviceHandle.hpp>
#include <Graphics/Texture.hpp>
#include <Util/Float16.hpp>
#include "Color.hpp"

namespace Ember
{
class MaterialManager;

TYPED_HANDLE( Material, MaterialManager );

enum class AlphaMode
{
  kOpaque,
  kMask,
  kBlend,
};

class MaterialImpl
{
public:
  struct alignas( 16 ) GpuRepr
  {
    SRVHandle BaseColorTexture;  // 04
    SRVHandle NormalTexture;     // 08
    SRVHandle MetalRoughTexture; // 12
    SRVHandle EmissiveTexture;   // 16
    Color32   BaseColorFactor;   // 20
    Color32   EmissiveFactor;    // 24 // Alpha component used for TexCoord
    Float16   EmissiveStrength;  // 26
    Float16   Metal;             // 28
    Float16   Rough;             // 30
    Float16   AlphaCutoff;       // 32
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
  AlphaMode            m_AlphaMode{ AlphaMode::kOpaque };

  std::atomic_uint32_t m_RefCount{ 1 };

public:
  uint32_t                     AddRef();
  uint32_t                     Release();
  uint32_t                     GetRefCount();

  [[nodiscard]] MaterialHandle GetHandle() const;
  [[nodiscard]] AlphaMode      GetAlphaMode() const;

  MaterialImpl(
      Texture          base_color_texture,
      Texture          normal_texture,
      Texture          metal_rough_texture,
      Texture          emissive_texture,
      MaterialManager* material_manager,
      MaterialHandle   handle,
      AlphaMode        alpha_mode );
};

} // namespace Ember
